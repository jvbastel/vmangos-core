/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_FOREVER_SPELL_IDS_H
#define WOWAI_GAME_FOREVER_SPELL_IDS_H

// The spell IDs that the server code of the Forever rules (spec jvbastel/WOWAI#204) knows by number, in one
// place. The spec file forever/warrior.tsv is the source of truth: each constant names its row with a
// "spec:" comment (the kind and the name of the row; "sources" for the sourceSpells column, else the
// spells column), and forever/generator/tests/test_spell_ids.py fails when a constant and its row differ.
// So keep the comment format when you add a constant. A talent array lists rank 1 first.
//
// No game header, so wowai/tests/forever-rules.test.sh tests it on the host.

#include <cstddef>
#include <cstdint>

namespace Forever
{
namespace SpellIds
{
    // The free ID range of the spec (its "# Free ID range" header line): new talents, spells and skill
    // line abilities. The stock 1.12.1 data has no spell here.
    constexpr uint32_t FreeIdFirst = 60000;  // spec: free range first
    constexpr uint32_t FreeIdLast = 65535;   // spec: free range last

    // Arms (#217).
    constexpr uint32_t ImprovedTacticalMasteryRanks[] = { 12295, 12676, 12677, 12678, 12679 };  // spec: talent Improved Tactical Mastery
    constexpr uint32_t SpearingStrikeSpell = 60100;                                                  // spec: talent Spearing Strike
    constexpr uint32_t BloodthrillRanks[] = { 60101, 60102, 60103, 60104, 60105 };              // spec: talent Bloodthrill
    constexpr uint32_t WeaponmasterRanks[] = { 12281, 12812, 12813, 12814, 12815 };             // spec: talent Weaponmaster
    constexpr uint32_t ImprovedSlamRanks[] = { 12862, 12330 };                                  // spec: talent Improved Slam

    // Warrior mechanics (#214, client 70685): Anger Management (Arms) and Dual Wield Specialization (Fury).
    constexpr uint32_t AngerManagementRanks[] = { 12296 };                                      // spec: talent Anger Management
    constexpr uint32_t DualWieldSpecializationRanks[] = { 23584, 23585, 23586, 23587, 23588 };  // spec: talent Dual Wield Specialization

    // Fury (#218).
    constexpr uint32_t LingeringRageRanks[] = { 60106, 60107, 60108, 60109, 60110 };            // spec: talent Lingering Rage
    constexpr uint32_t FuriousPrecisionRanks[] = { 60111, 60112, 60113 };                       // spec: talent Furious Precision
    constexpr uint32_t BloodCrazeRanks[] = { 16487, 16489, 16492 };                             // spec: talent Blood Craze
    constexpr uint32_t ImprovedBerserkerRageRanks[] = { 20500, 20501 };                         // spec: talent Improved Berserker Rage
    constexpr uint32_t GoreDrinkerRanks[] = { 60115, 60116 };                                   // spec: talent Gore Drinker
    // The buffs that the core gives: no rage decay while Lingering Rage lasts; Gore Drinker, 3 charges.
    constexpr uint32_t LingeringRageBuff = 60131;                                               // spec: ability Lingering Rage
    constexpr uint32_t GoreDrinkerBuff = 60132;                                                 // spec: ability Gore Drinker
    constexpr uint32_t BerserkerRageSpell = 18499;                                                   // spec: base Berserker Rage

    // Protection (#219).
    constexpr uint32_t IronWillRanks[] = { 12300, 12959, 12960, 12961, 12962 };                 // spec: talent Iron Will
    constexpr uint32_t MasterOfDefenseRanks[] = { 60117, 60118 };                               // spec: talent Master of Defense
    constexpr uint32_t DefianceRanks[] = { 12303, 12788, 12789 };                               // spec: talent Defiance
    constexpr uint32_t BastionRanks[] = { 16538, 16539, 16540, 16541, 16542 };                  // spec: talent Bastion
    // The 5 Rage of Shield Specialization and Master of Defense.
    constexpr uint32_t ShieldSpecializationRage = 23602;                                        // spec: base Shield Specialization

    // Vanguard (#216): the talent, and the Vanguard Charge of each stock Charge rank.
    constexpr uint32_t VanguardTalent = 60119;                                                  // spec: talent Vanguard
    constexpr uint32_t StockChargeRanks[] = { 100, 6178, 11578 };                               // spec: ability Charge sources
    constexpr uint32_t VanguardChargeRanks[] = { 60127, 60128, 60129 };                         // spec: ability Charge
    constexpr std::size_t ChargeRankCount = sizeof(StockChargeRanks) / sizeof(StockChargeRanks[0]);
    static_assert(ChargeRankCount == sizeof(VanguardChargeRanks) / sizeof(VanguardChargeRanks[0]),
        "each stock Charge rank has its Vanguard Charge");

    // Victory Rush (#213).
    constexpr uint32_t VictoryRushSpell = 60123;                                                     // spec: ability Victory Rush
    constexpr uint32_t VictoriousSpell = 60124;                                                      // spec: ability Victorious

    // A spell of the free range: it exists only in the Forever tables, so only with the rules on.
    constexpr bool IsForeverOnlySpell(uint32_t spellId)
    {
        return spellId >= FreeIdFirst && spellId <= FreeIdLast;
    }

    // At load, the stock core deletes the character_spell rows of a spell that does not exist, for all
    // characters (Player::AddSpell, and CharacterDatabaseCleaner). With the rules off, a Forever-only
    // spell does not exist; its rows stay, so that a Warrior who did not log in while the rules were off
    // still has its Forever spells and talents when the rules are on again (review S1). With the rules on,
    // an unknown Forever spell is one that the spec dropped, and the stock cleanup goes on.
    constexpr bool KeepsRowsOfUnknownSpell(uint32_t spellId, bool rulesOn)
    {
        return !rulesOn && IsForeverOnlySpell(spellId);
    }

    // The spell `id` is one of `ids`.
    template <std::size_t N>
    constexpr bool Contains(uint32_t const (&ids)[N], uint32_t id)
    {
        for (std::size_t i = 0; i < N; ++i)
            if (ids[i] == id)
                return true;
        return false;
    }

    // The rank (1 = rank 1) of the highest rank spell of `ranks` (rank 1 first) that `knows` is true for,
    // or 0.
    template <std::size_t N, typename Knows>
    uint32_t KnownRank(uint32_t const (&ranks)[N], Knows knows)
    {
        for (std::size_t i = N; i > 0; --i)
            if (knows(ranks[i - 1]))
                return uint32_t(i);
        return 0;
    }
}
}

#endif
