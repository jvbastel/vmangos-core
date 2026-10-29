/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_FOREVER_WARRIOR_RULES_H
#define WOWAI_GAME_FOREVER_WARRIOR_RULES_H

// The pure math of the Forever Warrior mechanics (jvbastel/WOWAI#214): rage from weapon speed and the
// Deep Wounds bleed. No game header, so wowai/tests/forever-warrior-rules.test.sh tests it on the host.
// ForeverWarrior.cpp calls it from the core hooks. The chosen formulas and their sources are in the
// header of forever/warrior.tsv ("Warrior mechanics").

#include <cstdint>

namespace Forever
{
namespace WarriorRules
{
    // Rage per second of base weapon speed (community measurement of the Forever beta, Icy Veins).
    constexpr float OneHandRagePerSecond = 3.46f;
    constexpr float TwoHandRagePerSecond = 4.50f;
    // Our choice (no published value): an off-hand hit gives half, as the normalized rage of TBC.
    constexpr float OffHandFactor = 0.5f;
    // Blizzard, 2 Oct: a critical basic attack gives 100% more rage.
    constexpr float CritFactor = 2.0f;
    // A Warrior without a usable weapon swings with the unarmed speed of the core (BASE_ATTACK_TIME).
    constexpr uint32_t UnarmedSpeedMs = 2000;
    // Deep Wound (12721): 12 sec with a tick every 3 sec.
    constexpr uint32_t DeepWoundTicks = 4;

    // The rage (in rage points, not tenths) of one melee swing that hits. Only the base speed of the
    // weapon counts, so haste gives more swings but not more rage per swing. Level and damage do not count.
    inline float SwingRage(uint32_t weaponSpeedMs, bool twoHand, bool offHand, bool crit)
    {
        float rage = float(weaponSpeedMs) / 1000.0f * (twoHand ? TwoHandRagePerSecond : OneHandRagePerSecond);
        if (offHand)
            rage *= OffHandFactor;
        if (crit)
            rage *= CritFactor;
        return rage;
    }

    // The damage of a Warrior DoT tick that crits: x2 (melee crit), then the crit damage talents
    // (`extraBonusPct`, for example Impale +10/20) add a percent of the normal tick.
    inline float DotCritDamage(float tickDamage, float extraBonusPct)
    {
        return tickDamage * (2.0f + extraBonusPct / 100.0f);
    }

    // The whole damage of a new Deep Wound: `pct` (20/40/60) of the weapon's average damage. Forever:
    // no attack power, so the weapon's own damage, not the unit's damage fields.
    inline float DeepWoundsDamage(float weaponMin, float weaponMax, float pct)
    {
        return (weaponMin + weaponMax) / 2.0f * pct / 100.0f;
    }

    // Forever's Deep Wounds rolls: a new bleed adds the damage that the old one had not dealt yet, and
    // spreads both over the full duration. `oldTick` is the damage of one old tick, `oldTicksLeft` the
    // ticks it still had. The tick damage of the new bleed.
    inline float DeepWoundsRolledTick(float newDamage, float oldTick, uint32_t oldTicksLeft)
    {
        float const left = oldTicksLeft > DeepWoundTicks ? float(DeepWoundTicks) : float(oldTicksLeft);
        float const old = oldTick > 0.0f ? oldTick * left : 0.0f;
        return (newDamage + old) / float(DeepWoundTicks);
    }
}
}

#endif
