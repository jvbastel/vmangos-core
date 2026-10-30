/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_FOREVER_ARMS_RULES_H
#define WOWAI_GAME_FOREVER_ARMS_RULES_H

// The pure rules of the Forever Arms talents (jvbastel/WOWAI#217): Improved Tactical Mastery, Spearing
// Strike, Bloodthrill, Weaponmaster and Improved Slam. No game header, so
// wowai/tests/forever-rules.test.sh tests it on the host. ForeverArms.cpp calls it from the core
// hooks. The values are in forever/spec/warrior.tsv (client build 1.60.1.70685).

#include <cstdint>

namespace Forever
{
namespace ArmsRules
{
    // Creature types of the core (CreatureDefines.h).
    constexpr uint32_t CreatureTypeDragonkin = 2;
    constexpr uint32_t CreatureTypeGiant = 5;

    // Bloodthrill: Overpower stays usable for 5 sec (client 1282733; the deep dive says 6 sec).
    constexpr uint32_t BloodthrillWindowMs = 5000;

    // The Rage (in tenths) that a Warrior has after a stance change. `baseKeptTenths` is what the
    // baseline Tactical Mastery keeps (100, the stock class script 832); Improved Tactical Mastery
    // keeps `improvedRage` (3/6/9/12/15, in Rage points) more.
    inline uint32_t RageAfterStanceChange(uint32_t currentTenths, uint32_t baseKeptTenths, int32_t improvedRage)
    {
        int64_t const cap = int64_t(baseKeptTenths) + int64_t(improvedRage) * 10;
        uint32_t const kept = cap > 0 ? uint32_t(cap) : 0;
        return currentTenths > kept ? kept : currentTenths;
    }

    // Spearing Strike deals its bonus to Giants, Dragonkin and mounted targets.
    inline bool IsSpearingBonusTarget(uint32_t creatureType, bool mounted)
    {
        return mounted || creatureType == CreatureTypeGiant || creatureType == CreatureTypeDragonkin;
    }

    // The factor on the 40% weapon damage of Spearing Strike. `bonusFactor` is effect 3 of the spell (2):
    // the additional 80% is two more times the 40%.
    inline float SpearingStrikeMultiplier(bool bonusTarget, int32_t bonusFactor)
    {
        return bonusTarget && bonusFactor > 0 ? 1.0f + float(bonusFactor) : 1.0f;
    }

    // Bloodthrill rolls for a melee attack of the main hand (a swing, or an ability such as Heroic Strike
    // or Cleave) that lands on a target with the Warrior's own Rend.
    inline bool BloodthrillCounts(bool mainHand, bool landed, bool targetHasOwnRend)
    {
        return mainHand && landed && targetHasOwnRend;
    }

    enum class WeaponmasterBenefit
    {
        None,
        Crit,           // Axe, Polearm: + crit chance (effect 3)
        ArmorIgnore,    // Mace, Staff: ignore a percent of the target's armor (effect 2)
        ExtraAttack,    // Sword: the stock Sword Specialization proc (effect 1)
    };

    // The Weaponmaster benefit of a weapon by its item subclass (ItemPrototype.h ITEM_SUBCLASS_WEAPON_*).
    inline WeaponmasterBenefit WeaponmasterBenefitOf(uint32_t weaponSubClass)
    {
        switch (weaponSubClass)
        {
            case 0:  // axe
            case 1:  // two-hand axe
            case 6:  // polearm
                return WeaponmasterBenefit::Crit;
            case 4:  // mace
            case 5:  // two-hand mace
            case 10: // staff
                return WeaponmasterBenefit::ArmorIgnore;
            case 7:  // sword
            case 8:  // two-hand sword
                return WeaponmasterBenefit::ExtraAttack;
            default:
                return WeaponmasterBenefit::None;
        }
    }

    // The armor that counts after Weaponmaster ignores `ignorePct` of it.
    inline float ArmorAfterIgnore(float armor, float ignorePct)
    {
        if (armor <= 0.0f)
            return armor;
        return armor * (100.0f - ignorePct) / 100.0f;
    }

    // Improved Slam: Slam no longer stops or resets the melee swing.
    inline bool SlamKeepsSwing(bool castIsSlam, bool hasImprovedSlam)
    {
        return castIsSlam && hasImprovedSlam;
    }
}
}

#endif
