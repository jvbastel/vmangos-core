/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_FOREVER_VICTORY_RUSH_RULES_H
#define WOWAI_GAME_FOREVER_VICTORY_RUSH_RULES_H

// The pure rules of Victory Rush under the Forever rules (jvbastel/WOWAI#213). No game header, so
// wowai/tests/forever-rules.test.sh tests it on the host. VictoryRush.cpp calls it from the core
// hooks. The spell rows are in forever/warrior.tsv (Victory Rush, Victorious).

#include <cstdint>

namespace Forever
{
namespace VictoryRushRules
{
    // The spec IDs of forever/warrior.tsv (Forever client spells 402927 and 402975).
    constexpr uint32_t VictoryRushSpell = 60123;
    constexpr uint32_t VictoriousSpell = 60124;
    // The caster aura state that Victory Rush needs, in its server row and in its client Spell.dbc row.
    // The core sets it while the Warrior has Victorious. 6 is free: the 1.12.1 core does not use it
    // (SpellDefines.h: AURA_STATE_UNKNOWN6, "not used"), no stock spell needs it, and the core sets
    // states only by name (the custom server states are 9-11). The client sees it in UNIT_FIELD_AURASTATE.
    constexpr uint32_t VictoriousAuraState = 6;

    // A kill, as Unit::Kill sees it for the unit that dealt the killing blow.
    struct Kill
    {
        bool rulesOn = false;               // Forever.Rules = 1
        bool killerIsWarriorPlayer = false;
        bool killerKnowsVictoryRush = false; // learned at level 20 (#212)
        bool killedSelf = false;
        bool honorOrXpTarget = false;       // Player::IsHonorOrXPTarget: not grey, not a totem or pet, ...
    };

    // The killer gets Victorious (and so may use Victory Rush for 20 sec).
    inline bool GrantsVictorious(Kill const& kill)
    {
        return kill.rulesOn && kill.killerIsWarriorPlayer && kill.killerKnowsVictoryRush && !kill.killedSelf &&
               kill.honorOrXpTarget;
    }

    // The damage of Victory Rush: the flat value of effect 1 plus `attackPowerPercent` (effect 3) of the
    // attack power, rounded down.
    inline int32_t Damage(int32_t flat, float attackPower, int32_t attackPowerPercent)
    {
        return flat + int32_t(attackPower * float(attackPowerPercent) / 100.0f);
    }

    // The heal of Victory Rush: `percent` (effect 2) of the maximum health, at least 1.
    inline uint32_t Heal(uint32_t maxHealth, int32_t percent)
    {
        uint32_t heal = uint32_t(uint64_t(maxHealth) * uint32_t(percent > 0 ? percent : 0) / 100);
        return heal > 0 ? heal : 1;
    }
}
}

#endif
