/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// Vanguard (spec jvbastel/WOWAI#204, WOWAI#216): "Your Charge ability is now usable while in Defensive
// Stance." The client checks the stance of a spell itself, so the talent cannot change Charge. It swaps
// Charge for Vanguard Charge instead: a copy of the same rank that the client allows in Battle and
// Defensive Stance (forever/warrior.tsv, the ability row Charge).
//
// While a Warrior knows the Vanguard talent, the spellbook shows the Vanguard Charge of its highest
// stock Charge rank in place of stock Charge, and each Charge button casts it. The stock Charge ranks
// stay known but inactive (not in the spellbook), so that the trainer still sees the ranks that the
// Warrior has and the swap back knows the rank. Without the talent, stock Charge comes back on the
// same buttons.

#include "Forever.h"
#include "ForeverSpellIds.h"
#include "Database/DatabaseEnv.h"
#include "MasterPlayer.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldSession.h"

#include <iterator>
#include <memory>
#include <string>
#include <unordered_set>

namespace
{
    using namespace Forever::SpellIds;

    // A stock Charge rank (StockChargeRanks) and its Vanguard Charge (VanguardChargeRanks) have the same
    // index, rank 1 first.

    // The highest stock Charge rank that the player knows (active or not), from 0, or -1.
    int TopStockRank(Player const* player)
    {
        return int(KnownRank(StockChargeRanks, [player](uint32 id) { return player->HasSpell(id); })) - 1;
    }

    // The highest Vanguard Charge that the player knows, or 0.
    uint32 KnownVanguardCharge(Player const* player)
    {
        uint32 const rank = KnownRank(VanguardChargeRanks, [player](uint32 id) { return player->HasSpell(id); });
        return rank ? VanguardChargeRanks[rank - 1] : 0;
    }

    // Makes a known spell active (in the spellbook) or inactive, as a saved change. Player::AddSpell does
    // the same for a known spell, but it is private and sends its own packets.
    void SetActive(Player* player, uint32 spellId, bool active)
    {
        auto const itr = player->GetSpellMap().find(spellId);
        if (itr == player->GetSpellMap().end() || itr->second.state == PLAYERSPELL_REMOVED || itr->second.active == active)
            return;
        itr->second.active = active;
        if (itr->second.state != PLAYERSPELL_NEW)
            itr->second.state = PLAYERSPELL_CHANGED;
    }

    // Learns a Vanguard Charge with no packet (the caller sends one) and no rank logic: it has no spell
    // chain, no skill line and no effect that teaches something, so this is all that Player::AddSpell
    // does for it.
    void LearnQuietly(Player* player, uint32 spellId)
    {
        auto const itr = player->GetSpellMap().find(spellId);
        PlayerSpell spell;
        spell.state = itr != player->GetSpellMap().end() ? PLAYERSPELL_CHANGED : PLAYERSPELL_NEW;
        spell.active = true;
        spell.dependent = false;
        spell.disabled = false;
        player->GetSpellMap()[spellId] = spell;
    }

    std::unordered_set<uint32> VanguardChargeSet()
    {
        return std::unordered_set<uint32>(std::begin(VanguardChargeRanks), std::end(VanguardChargeRanks));
    }

    // Each button of a spell in `from` casts `to` instead.
    void ConvertButtons(MasterPlayer* master, std::unordered_set<uint32> const& from, uint32 to)
    {
        if (!master)
            return;
        for (auto& button : master->GetActionButtons())
        {
            ActionButton& action = button.second;
            if (action.uState != ACTIONBUTTON_DELETED && action.GetType() == ACTION_BUTTON_SPELL &&
                from.count(action.GetAction()))
                action.SetActionAndType(to, ACTION_BUTTON_SPELL);
        }
    }

    // The client replaces `from` with `to` in its spellbook and on its action bars.
    void SendReplaced(Player* player, uint32 from, uint32 to)
    {
        if (!player->IsInWorld())
            return;
        if (from)
        {
            auto packet = std::make_unique<WorldPackets::Spell::SupercededSpell>();
            packet->oldSpellId = from;
            packet->newSpellId = to;
            player->GetSession()->SendPacket(std::move(packet));
        }
        else
        {
            auto packet = std::make_unique<WorldPackets::Spell::LearnedSpell>();
            packet->spellId = to;
            player->GetSession()->SendPacket(std::move(packet));
        }
    }

    // With the talent: the Vanguard Charge of the highest stock rank in place of every Charge spell.
    void GiveVanguardCharge(Player* player)
    {
        int const top = TopStockRank(player);
        if (top < 0)
            return;
        uint32 const target = VanguardChargeRanks[top];

        // The Charge that the client shows now, and so the one its buttons hold: a Vanguard Charge of a
        // lower rank, else the stock rank.
        bool const known = player->HasSpell(target);
        uint32 shown = KnownVanguardCharge(player);
        if (shown == target)
            shown = 0;
        if (!shown && !known && player->HasActiveSpell(StockChargeRanks[top]))
            shown = StockChargeRanks[top];

        // The packet goes first: the spell removals below then remove what the client no longer has.
        if (shown || !known)
            SendReplaced(player, shown, target);

        std::unordered_set<uint32> from(std::begin(StockChargeRanks), std::end(StockChargeRanks));
        for (uint32 vanguard : VanguardChargeRanks)
            if (vanguard != target)
                from.insert(vanguard);
        ConvertButtons(player->GetSession()->GetMasterPlayer(), from, target);

        if (!player->HasSpell(target))
            LearnQuietly(player, target);
        for (uint32 vanguard : VanguardChargeRanks)
            if (vanguard != target && player->HasSpell(vanguard))
                player->RemoveSpell(vanguard, false, false);
        for (uint32 stock : StockChargeRanks)
        {
            if (player->HasActiveSpell(stock))
            {
                // A stock rank that the client still shows: one that a trainer re-activated as the
                // lower rank of a new one.
                if (stock != shown && player->IsInWorld())
                    player->SendSpellRemoved(stock);
                SetActive(player, stock, false);
            }
        }
    }

    // Without the talent: the highest stock Charge rank in place of the Vanguard Charge.
    void GiveBackStockChargeInWorld(Player* player)
    {
        uint32 const shown = KnownVanguardCharge(player);
        if (!shown)
            return;
        int const top = TopStockRank(player);
        if (top >= 0)
        {
            uint32 const stock = StockChargeRanks[top];
            SendReplaced(player, shown, stock);
            // At load (a talent reset in Player::LoadFromDB) there is no MasterPlayer yet: the login
            // converts the buttons later (Forever::FixChargeButtons).
            ConvertButtons(player->GetSession()->GetMasterPlayer(), VanguardChargeSet(), stock);
            SetActive(player, stock, true);
        }
        for (uint32 vanguard : VanguardChargeRanks)
            if (player->HasSpell(vanguard))
                player->RemoveSpell(vanguard, false, false);
    }
}

void Forever::OnSpellLearned(Player* player, uint32 spellId)
{
    if (!IsForeverWarrior(player))
        return;
    if (spellId != VanguardTalent && !Contains(StockChargeRanks, spellId))
        return;
    if (player->HasSpell(VanguardTalent))
        GiveVanguardCharge(player);
}

void Forever::OnSpellRemoved(Player* player, uint32 spellId)
{
    if (spellId != VanguardTalent || !IsForeverWarrior(player))
        return;
    GiveBackStockChargeInWorld(player);
}

void Forever::GiveBackStockCharge(Player* player)
{
    // Under the vanilla rules the core does not know the Vanguard Charge spells: Player::AddSpell has not
    // loaded them (their rows stay, KeepsRowsOfUnknownSpell), and MasterPlayer::LoadActions has marked
    // their buttons deleted. The stock
    // ranks are still known, the highest one inactive.
    int const top = TopStockRank(player);
    if (top < 0)
        return;
    uint32 const stock = StockChargeRanks[top];
    if (player->HasActiveSpell(stock))
        return;
    SetActive(player, stock, true);

    // The buttons of the Vanguard Charge are still in the database until the switch saves the bars.
    std::string vanguardCharges;
    for (uint32 vanguard : VanguardChargeRanks)
        vanguardCharges += (vanguardCharges.empty() ? "" : ", ") + std::to_string(vanguard);
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
        "SELECT `button` FROM `character_action` WHERE `guid` = %u AND `type` = %u AND `action` IN (%s)",
        player->GetGUIDLow(), uint32(ACTION_BUTTON_SPELL), vanguardCharges.c_str()));
    MasterPlayer* master = player->GetSession()->GetMasterPlayer();
    if (!result || !master)
        return;
    do
        master->addActionButton(result->Fetch()[0].GetUInt8(), stock, ACTION_BUTTON_SPELL);
    while (result->NextRow());
}

void Forever::FixChargeButtons(Player* player, MasterPlayer* master)
{
    if (!IsForeverWarrior(player) || player->HasSpell(VanguardTalent))
        return;
    // A talent reset in Player::LoadFromDB took Vanguard away before the action bars were loaded: the
    // buttons of its Vanguard Charge get the stock Charge that came back (review S4).
    int const top = TopStockRank(player);
    if (top >= 0)
        ConvertButtons(master, VanguardChargeSet(), StockChargeRanks[top]);
}
