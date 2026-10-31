/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_FOREVER_PROTECTION_RULES_H
#define WOWAI_GAME_FOREVER_PROTECTION_RULES_H

// The pure rules of the Forever Protection talents (jvbastel/WOWAI#219): which spells they are, when the
// shield talents are active, when Master of Defense can give Rage, and the Iron Will duration. No game
// header, so wowai/tests/forever-rules.test.sh tests it on the host. ForeverProtection.cpp
// calls it from the core hooks. The spell IDs and values are the rows of forever/spec/warrior.tsv.

#include "ForeverSpellIds.h"

#include <cstddef>
#include <cstdint>

namespace Forever
{
namespace ProtectionRules
{
    using namespace SpellIds;

    // Defiance (3 ranks) and Bastion (5 ranks): passive auras that work only while a shield is equipped.
    constexpr uint32_t ShieldTalentSpells[] = { DefianceRanks[0], DefianceRanks[1], DefianceRanks[2],
        BastionRanks[0], BastionRanks[1], BastionRanks[2], BastionRanks[3], BastionRanks[4] };
    static_assert(sizeof(ShieldTalentSpells) == sizeof(DefianceRanks) + sizeof(BastionRanks),
        "every Defiance and Bastion rank is a shield talent spell");
    // The Rage of Shield Specialization and Master of Defense: 5 Rage (the base row Shield Specialization).
    constexpr uint32_t MasterOfDefenseRageSpell = ShieldSpecializationRage;

    // Master of Defense: passive dummy auras, effect 1 = the chance (50/100) to gain Rage on a dodge or
    // parry. Iron Will: passive dummy auras, effect 1 = the percent (3-15) off the duration of Charm and Stun.
    inline bool IsShieldTalentSpell(uint32_t spellId) { return Contains(ShieldTalentSpells, spellId); }
    inline bool IsMasterOfDefenseSpell(uint32_t spellId) { return Contains(MasterOfDefenseRanks, spellId); }
    inline bool IsIronWillSpell(uint32_t spellId) { return Contains(IronWillRanks, spellId); }

    // A shield talent aura is on while a usable shield is equipped and the Warrior is in the stance that
    // the spell asks for (Defiance: Defensive Stance; Bastion: any).
    inline bool ShieldTalentActive(bool shieldEquipped, bool stanceFits)
    {
        return shieldEquipped && stanceFits;
    }

    // Master of Defense rolls its chance only on a dodge or parry, with a shield, and with the talent.
    inline bool MasterOfDefenseCanProc(bool dodgeOrParry, bool shieldEquipped, int32_t chancePct)
    {
        return dodgeOrParry && shieldEquipped && chancePct > 0;
    }

    // The mechanics that Iron Will shortens (client 70685: MECHANIC_DURATION_MOD, misc 1 and 12), as bits of
    // a spell mechanic mask (bit = 1 << (mechanic - 1), SpellEntry::GetAllSpellMechanicMask).
    constexpr uint32_t MechanicCharm = 1;
    constexpr uint32_t MechanicStun = 12;
    constexpr uint32_t IronWillMechanicMask = (1u << (MechanicCharm - 1)) | (1u << (MechanicStun - 1));

    inline bool IronWillShortens(uint32_t spellMechanicMask)
    {
        return (spellMechanicMask & IronWillMechanicMask) != 0;
    }

    // The duration (ms) of a Charm or Stun effect on a Warrior with Iron Will (`reductionPct` off, rounded
    // down). An endless effect (-1 or 0) stays as it is. The percent is held to 0-100.
    inline int32_t IronWillDuration(int32_t durationMs, int32_t reductionPct)
    {
        if (durationMs <= 0)
            return durationMs;
        int64_t const pct = reductionPct < 0 ? 0 : (reductionPct > 100 ? 100 : reductionPct);
        return int32_t(int64_t(durationMs) * (100 - pct) / 100);
    }
}
}

#endif
