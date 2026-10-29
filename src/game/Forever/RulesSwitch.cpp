/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// The rules switch at login (spec jvbastel/WOWAI#204, WOWAI#208): a restart with Forever.Rules turned on
// or off is enough to move each Warrior to the active rules, with no database script by hand.

#include "Forever.h"
#include "Database/DatabaseEnv.h"
#include "Database/DBCStore.h"
#include "DBCStructure.h"
#include "DBCfmt.h"
#include "Log.h"
#include "MasterPlayer.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellMgr.h"

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

    // The steps of the switch of a Warrior to the rules `to`. The extension point of the switch: a later
    // step goes here as one call, before the action bars are cleaned (OnLogin), so that a button of a
    // spell that a step takes away leaves the bars too.
    //   #212: with to == Rules::Forever, learn the baseline Forever abilities of the level.
    //   #216: with to == Rules::Vanilla, give back stock Charge.
    void SwitchWarrior(Player* player, Rules to)
    {
        // The talents of the active trees (sTalentStore). Spells of Forever-only IDs never get here
        // under the vanilla rules: Player::AddSpell drops a character_spell row of an unknown spell at
        // load.
        player->ResetTalents(true);

        // Under the Forever rules, the stock Warrior talents are no talents of the talent store, so
        // ResetTalents leaves them. They are not in the Forever rule set.
        if (to == Rules::Forever)
            for (StockTalentSpell const& talent : stockTalentSpells)
                if (talent.classMask & player->GetClassMask())
                    UnlearnTalentSpell(player, talent.spell);
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
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Forever] Cannot read the stock Talent.dbc and TalentTab.dbc in %s: the rules switch keeps the vanilla talents.", stockDbcPath.c_str());
        return;
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

void Forever::OnLogin(Player* player, MasterPlayer* master)
{
    uint32 const guid = player->GetGUIDLow();
    bool found = false;
    Rules const from = StoredRules(guid, found);
    Rules const to = ActiveRules();
    if (found && from == to)
        return;

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
        // SaveToDB wrote `online` = 0 (the player is not in the world yet). HandlePlayerLogin sets it to
        // 1 later, but in another queue, so it can run first. Set it here too, last in this queue.
        CharacterDatabase.PExecute("UPDATE `characters` SET `online` = 1 WHERE `guid` = %u", guid);
    }
    CharacterDatabase.PExecute("REPLACE INTO `forever_character` (`guid`, `rules`) VALUES (%u, %u)", guid, uint32(to));
    CharacterDatabase.CommitTransaction();
}
