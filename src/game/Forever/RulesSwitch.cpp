/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// The rules switch at login (spec jvbastel/WOWAI#204, WOWAI#208): a restart with Forever.Rules turned on
// or off is enough to move each Warrior to the active rules, with no database script by hand.

#include "Forever.h"
#include "ForeverProfessionIds.h"
#include "ForeverSpellIds.h"
#include "Database/DatabaseEnv.h"
#include "Database/DBCStore.h"
#include "DBCStructure.h"
#include "DBCfmt.h"
#include "DBCStores.h"
#include "Log.h"
#include "MasterPlayer.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellMgr.h"

#include <cstdlib>
#include <memory>
#include <unordered_set>
#include <vector>

namespace
{
    // The rules of a character: the `rules` column of the characters table forever_character.
    enum class Rules : uint8
    {
        Vanilla = 0,
        Forever = 1,
    };

    // A rank spell of a stock talent, and the classes of its tree.
    struct StockTalentSpell
    {
        uint32 classMask;
        uint32 spell;
    };

    // Filled only with the rules on (LoadStockTalents).
    std::vector<StockTalentSpell> stockTalentSpells;

    char const* NameOf(Rules rules)
    {
        return rules == Rules::Forever ? "Forever" : "vanilla";
    }

    Rules ActiveRules()
    {
        return Forever::RulesOn() ? Rules::Forever : Rules::Vanilla;
    }

    // The rules that the character last logged in under. `found` is false when it has no row: it has not
    // logged in since the migration of forever_character, so it last played under the vanilla rules.
    Rules StoredRules(uint32 guid, bool& found)
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT `rules` FROM `forever_character` WHERE `guid` = %u", guid));
        found = bool(result);
        return result && result->Fetch()[0].GetUInt8() == uint8(Rules::Forever) ? Rules::Forever : Rules::Vanilla;
    }

    // The spells that the spellbook shows: the ones whose buttons stay valid.
    std::unordered_set<uint32> KnownSpells(Player const* player)
    {
        std::unordered_set<uint32> known;
        for (auto const& spell : player->GetSpellMap())
            if (player->HasSpell(spell.first))
                known.insert(spell.first);
        return known;
    }

    // Unlearns one talent rank spell the way Player::ResetTalents does, also when the talent store does
    // not know it as a talent.
    void UnlearnTalentSpell(Player* player, uint32 spellId)
    {
        if (!player->GetSpellMap().count(spellId))
            return;
        if (SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId))
            for (uint32 triggered : spell->EffectTriggerSpell)
                if (triggered)
                    player->RemoveAurasDueToSpell(triggered);
        player->RemoveSpell(spellId, false, false);
    }

    // Under the vanilla rules: the Warrior spells that the Forever rules teach below their stock level
    // (Berserker Rage at 30, stock 32 at the trainer) go until the Warrior reaches that level (#208,
    // user story 21). Talents and spells that another spell teaches are not trainer spells.
    void UnlearnAboveStockLevel(Player* player)
    {
        std::vector<uint32> above;
        for (auto const& spell : player->GetSpellMap())
        {
            if (spell.second.state == PLAYERSPELL_REMOVED || spell.second.dependent || GetTalentSpellPos(spell.first))
                continue;
            SpellEntry const* entry = sSpellMgr.GetSpellEntry(spell.first);
            if (entry && entry->SpellFamilyName == SPELLFAMILY_WARRIOR && entry->spellLevel > player->GetLevel())
                above.push_back(spell.first);
        }
        for (uint32 spellId : above)
            player->RemoveSpell(spellId, false, false);
    }

    // The steps of the switch of a Warrior to the rules `to`. The extension point of the switch: a later
    // step goes here as one call, before the action bars are cleaned (OnLogin), so that a button of a
    // spell that a step takes away leaves the bars too.
    //   #212: with to == Rules::Forever, learn the baseline Forever abilities of the level (done below).
    //   #216: with to == Rules::Vanilla, give back stock Charge.
    void SwitchWarrior(Player* player, Rules to)
    {
        // The talents of the active trees (sTalentStore). Spells of Forever-only IDs never get here
        // under the vanilla rules: Player::AddSpell does not load a spell that the core does not know
        // (its character_spell rows stay for the next Forever login, KeepsRowsOfUnknownSpell).
        player->ResetTalents(true);

        // Under the Forever rules, the stock Warrior talents are no talents of the talent store, so
        // ResetTalents leaves them. They are not in the Forever rule set.
        if (to == Rules::Forever)
            for (StockTalentSpell const& talent : stockTalentSpells)
                if (talent.classMask & player->GetClassMask())
                    UnlearnTalentSpell(player, talent.spell);

        if (to == Rules::Vanilla)
        {
            Forever::GiveBackStockCharge(player);
            UnlearnAboveStockLevel(player);
        }

        // #212: the baseline Forever abilities of the level, with no trainer visit.
        if (to == Rules::Forever)
            if (uint32 learned = Forever::LearnBaselineSpells(player))
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Forever] %s learns %u baseline spells of level %u.", player->GetName(), learned, player->GetLevel());
    }

    // Takes the buttons of the spells that the switch took away off the action bars. A button of a
    // spell that the server does not know at all is already gone: MasterPlayer::LoadActions marks it
    // deleted.
    uint32 RemoveLostButtons(Player const* player, MasterPlayer* master, std::unordered_set<uint32> const& knownBefore)
    {
        std::vector<uint8> lost;
        for (auto const& button : master->GetActionButtons())
        {
            ActionButton const& action = button.second;
            if (action.uState != ACTIONBUTTON_DELETED && action.GetType() == ACTION_BUTTON_SPELL &&
                knownBefore.count(action.GetAction()) && !player->HasSpell(action.GetAction()))
                lost.push_back(button.first);
        }
        for (uint8 button : lost)
            master->removeActionButton(button);
        return uint32(lost.size());
    }
}

void Forever::LoadStockTalents(std::string const& stockDbcPath)
{
    stockTalentSpells.clear();
    if (!RulesOn())
        return;

    DBCStorage<TalentTabEntry> tabs(TalentTabEntryfmt);
    DBCStorage<TalentEntry> talents(TalentEntryfmt);
    if (!tabs.Load((stockDbcPath + "TalentTab.dbc").c_str()) || !talents.Load((stockDbcPath + "Talent.dbc").c_str()))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Forever] Cannot read the stock Talent.dbc and TalentTab.dbc in %s. The rules switch needs them.", stockDbcPath.c_str());
        Log::WaitBeforeContinueIfNeed();
        exit(1);
    }

    for (uint32 i = 0; i < talents.GetNumRows(); ++i)
    {
        TalentEntry const* talent = talents.LookupEntry(i);
        if (!talent)
            continue;
        TalentTabEntry const* tab = tabs.LookupEntry(talent->TalentTab);
        if (!tab)
            continue;
        for (uint32 spell : talent->RankID)
            if (spell)
                stockTalentSpells.push_back({ tab->ClassMask, spell });
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Forever] Read %u stock talent spells for the rules switch.", uint32(stockTalentSpells.size()));
}

namespace Forever
{
namespace
{
    // The rules switch of OnLogin: compares the rules that the character last logged in under with the active
    // rules.
    void SwitchAtLogin(Player* player, MasterPlayer* master)
    {
        uint32 const guid = player->GetGUIDLow();
        bool found = false;
        Rules const from = StoredRules(guid, found);
        Rules const to = ActiveRules();
        if (found && from == to)
        {
            // A baseline spell that a refresh of the spec added since the last login (#212).
            LearnBaselineSpells(player);
            return;
        }

        // Only the Warrior has Forever rules; another class only gets its rules stored.
        bool const switched = from != to && player->GetClass() == CLASS_WARRIOR;
        if (switched)
        {
            std::unordered_set<uint32> const knownBefore = KnownSpells(player);
            SwitchWarrior(player, to);
            uint32 const buttons = RemoveLostButtons(player, master, knownBefore);
            uint32 spells = 0;
            for (uint32 spell : knownBefore)
                if (!player->HasSpell(spell))
                    ++spells;
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Forever] %s switches from the %s rules to the %s rules: talents reset, %u spells and %u action buttons removed.",
                player->GetName(), NameOf(from), NameOf(to), spells, buttons);
        }

        // To the client: the login sends the spells and the action bars after this call
        // (SendInitialPacketsBeforeAddToMap, SendInitialActionButtons), so it never sees the old ones.
        // To the database: a bot with saving off keeps nothing, so it switches again at its next login.
        // Else a switched character and its action bars are saved now, and then its rules, in the same
        // queue (serial id = guid) as Player::SaveToDB, so that a crash cannot store the new rules without
        // the switch.
        if (player->IsSavingDisabled())
            return;
        if (switched)
            player->SaveToDB();
        CharacterDatabase.BeginTransaction(guid);
        if (switched)
        {
            master->SaveActions();
            // The rows of the Forever-only spells that this character no longer has. The vanilla core did
            // not load them, so SaveToDB keeps them (KeepsRowsOfUnknownSpell); the next Forever login
            // starts from a talent reset and the baseline spells anyway. The profession spells stay: a recipe
            // is no Warrior spell, and it is back at the next Forever login (#233, WarriorSwitchOffDeletes).
            if (to == Rules::Vanilla)
                CharacterDatabase.PExecute("DELETE FROM `character_spell` WHERE `guid` = %u AND `spell` BETWEEN %u AND %u AND `spell` NOT BETWEEN %u AND %u",
                    guid, Forever::SpellIds::FreeIdFirst, Forever::SpellIds::FreeIdLast,
                    Forever::ProfessionIds::ProfessionSpellFirst, Forever::ProfessionIds::ProfessionSpellLast);
            // SaveToDB wrote `online` = 0 (the player is not in the world yet). HandlePlayerLogin sets it to
            // 1 later, but in another queue, so it can run first. Set it here too, last in this queue.
            CharacterDatabase.PExecute("UPDATE `characters` SET `online` = 1 WHERE `guid` = %u", guid);
        }
        CharacterDatabase.PExecute("REPLACE INTO `forever_character` (`guid`, `rules`) VALUES (%u, %u)", guid, uint32(to));
        CharacterDatabase.CommitTransaction();
    }
}
}

void Forever::OnLogin(Player* player, MasterPlayer* master)
{
    SwitchAtLogin(player, master);
    // After a talent reset at load and after the switch: Vanguard Charge buttons without the talent.
    FixChargeButtons(player, master);
    // Defiance and Bastion, now that the items are loaded (the spells load first, and the item mods at
    // load do not reach the off-hand hook).
    UpdateShieldTalents(player);
}
