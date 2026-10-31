/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_FOREVER_WARRIOR_RULES_H
#define WOWAI_GAME_FOREVER_WARRIOR_RULES_H

// The pure math of the Forever Warrior mechanics (jvbastel/WOWAI#214): rage from weapon speed, the rage
// of Anger Management and Dual Wield Specialization, the Deep Wounds bleed and the attack power part of
// Revenge. No game header, so wowai/tests/forever-rules.test.sh tests it on the host.
// ForeverWarrior.cpp calls it from the core hooks. The chosen formulas and their sources are in the
// header of forever/spec/warrior.tsv ("Warrior mechanics").

#include "ForeverSpellIds.h"

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
    // The rank spells of Dual Wield Specialization and Anger Management: ForeverSpellIds.h.
    using SpellIds::DualWieldSpecializationRanks;
    using SpellIds::AngerManagementRanks;
    // A Warrior without a usable weapon swings with the unarmed speed of the core (BASE_ATTACK_TIME).
    constexpr uint32_t UnarmedSpeedMs = 2000;
    // Revenge: a percent of attack power on top of the base damage. Only the client hotfix of Revenge has
    // it (SpellDescriptionVariables 1024: 0.225 in Defensive Stance, else 0.25; still pushed to build
    // 1.60.1.70685), not the 70685 DB2 itself. Improved Revenge raises the whole damage (a damage modifier).
    constexpr float RevengeAttackPowerPct = 25.0f;
    constexpr float RevengeDefensiveAttackPowerPct = 22.5f;
    // Deep Wound (12721): 12 sec with a tick every 3 sec.
    constexpr uint32_t DeepWoundTicks = 4;

    // The rage (in rage points, not tenths) of one melee swing that hits. Only the base speed of the
    // weapon counts, so haste gives more swings but not more rage per swing. Level and damage do not count.
    // `offHandRagePct`: Dual Wield Specialization (effect 2 of the rank, 10-50, client 70685) gives an
    // off-hand hit that much more rage.
    inline float SwingRage(uint32_t weaponSpeedMs, bool twoHand, bool offHand, bool crit, int32_t offHandRagePct = 0)
    {
        float rage = float(weaponSpeedMs) / 1000.0f * (twoHand ? TwoHandRagePerSecond : OneHandRagePerSecond);
        if (offHand)
            rage *= OffHandFactor * (1.0f + float(offHandRagePct > 0 ? offHandRagePct : 0) / 100.0f);
        if (crit)
            rage *= CritFactor;
        return rage;
    }

    // Anger Management (12296, client 70685): its rage regeneration ticks only in combat, and out of combat
    // the rage loss is `reductionPct` (effect 2 of the talent, 30) smaller.
    constexpr uint32_t AngerManagementSpell = AngerManagementRanks[0];

    inline bool AngerManagementTicks(bool inCombat)
    {
        return inCombat;
    }

    // The rage that one out-of-combat decay tick takes (`stockLoss`, in tenths) with `reductionPct` off.
    inline float RageDecay(float stockLoss, int32_t reductionPct)
    {
        int32_t const pct = reductionPct < 0 ? 0 : (reductionPct > 100 ? 100 : reductionPct);
        return stockLoss * float(100 - pct) / 100.0f;
    }

    // The attack power part of the Revenge damage.
    inline float RevengeAttackPowerBonus(float attackPower, bool defensiveStance)
    {
        if (attackPower <= 0.0f)
            return 0.0f;
        return attackPower * (defensiveStance ? RevengeDefensiveAttackPowerPct : RevengeAttackPowerPct) / 100.0f;
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
