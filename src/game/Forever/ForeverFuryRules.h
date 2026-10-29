/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_FOREVER_FURY_RULES_H
#define WOWAI_GAME_FOREVER_FURY_RULES_H

// The pure rules of the Forever Fury talents (jvbastel/WOWAI#218): the spell IDs of the talent ranks and
// the small decisions of Lingering Rage, Furious Precision, Blood Craze, Improved Berserker Rage and Gore
// Drinker. No game header, so wowai/tests/forever-fury-rules.test.sh tests it on the host. ForeverFury.cpp
// calls it from the core hooks. The talent values come from the rank spells (forever/warrior.tsv).

#include <cstddef>
#include <cstdint>

namespace Forever
{
namespace FuryRules
{
    // The rank spells of each talent, rank 1 first (forever/warrior.tsv).
    constexpr uint32_t LingeringRageRanks[] = { 60106, 60107, 60108, 60109, 60110 };
    constexpr uint32_t FuriousPrecisionRanks[] = { 60111, 60112, 60113 };
    constexpr uint32_t BloodCrazeRanks[] = { 16487, 16489, 16492 };
    constexpr uint32_t ImprovedBerserkerRageRanks[] = { 20500, 20501 };
    constexpr uint32_t GoreDrinkerRanks[] = { 60115, 60116 };

    // The buffs that the core gives (ability rows of forever/warrior.tsv).
    constexpr uint32_t LingeringRageBuff = 60131;   // no rage decay while it lasts
    constexpr uint32_t GoreDrinkerBuff = 60132;     // 3 charges, 8 sec

    constexpr uint32_t BerserkerRage = 18499;

    // The rank (1 = first) of the highest rank spell of `ranks` that `knows` is true for, or 0.
    template <std::size_t N, typename Knows>
    uint32_t KnownRank(uint32_t const (&ranks)[N], Knows knows)
    {
        for (std::size_t i = N; i > 0; --i)
            if (knows(ranks[i - 1]))
                return uint32_t(i);
        return 0;
    }

    // Lingering Rage: out of combat the rage decays, unless the delay of the talent still runs.
    inline bool RageDecays(bool inCombat, bool lingeringRageRunning)
    {
        return !inCombat && !lingeringRageRunning;
    }

    // Furious Precision: the hit chance bonus (percent) of one attack. Off-hand attacks only.
    inline int32_t OffHandHitBonus(bool offHand, int32_t talentPct)
    {
        return offHand && talentPct > 0 ? talentPct : 0;
    }

    // Blood Craze: a single attack that deals more than `pct` percent of the maximum health.
    inline bool BloodCrazeTriggers(uint32_t damage, uint32_t maxHealth, int32_t pct)
    {
        return maxHealth > 0 && pct > 0 && uint64_t(damage) * 100 > uint64_t(maxHealth) * uint64_t(pct);
    }

    // Gore Drinker: the heal of one melee attack, `tenthsPct` tenths of a percent of the maximum health
    // (rank 1: 5 = 0.5%, rank 2: 10 = 1%). At least 1 when the talent is there.
    inline uint32_t GoreDrinkerHeal(uint32_t maxHealth, int32_t tenthsPct)
    {
        if (tenthsPct <= 0 || maxHealth == 0)
            return 0;
        uint64_t const heal = uint64_t(maxHealth) * uint64_t(tenthsPct) / 1000;
        return heal > 0 ? uint32_t(heal) : 1;
    }

    // Gore Drinker: the spells whose cast gives the buff. Enrage (the buffs of the Enrage talent, ranks
    // 1-5), Berserker Rage, Bloodrage (the cast, not its Rage over time 29131), Death Wish, Bloodthirst
    // (the talent rank and ranks 2-4).
    inline bool IsGoreDrinkerTrigger(uint32_t spellId)
    {
        switch (spellId)
        {
            case 12880: case 14201: case 14202: case 14203: case 14204:    // Enrage
            case 18499:                                                     // Berserker Rage
            case 2687:                                                      // Bloodrage
            case 12328:                                                     // Death Wish
            case 23881: case 23892: case 23893: case 23894:                 // Bloodthirst
                return true;
            default:
                return false;
        }
    }

    // Improved Berserker Rage: `roll` is a random number in [0, 100); the snares go below the chance.
    inline bool RemovesSnares(int32_t chancePct, float roll)
    {
        return chancePct > 0 && roll < float(chancePct);
    }
}
}

#endif
