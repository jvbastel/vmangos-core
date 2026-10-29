/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_FOREVER_ARMS_H
#define WOWAI_GAME_FOREVER_ARMS_H

#include "Common.h"
#include "SharedDefines.h"

class Unit;
class SpellEntry;

// The Forever Arms talents that need server code (jvbastel/WOWAI#217): Improved Tactical Mastery,
// Spearing Strike, Bloodthrill, Weaponmaster and Improved Slam. Each function is one hook in an upstream
// file. Each does nothing (and returns the stock answer) unless the Forever rules are on and the unit is
// a Warrior player. The rules are in ForeverArmsRules.h, the values in the talent spells
// (forever/warrior.tsv).
namespace Forever
{
    // Aura::HandleAuraModShapeshift, Warrior stances: `keptTenths` is the Rage that the stock Tactical
    // Mastery class scripts keep (the baseline Tactical Mastery: 100). With the rules, Improved Tactical
    // Mastery keeps 3/6/9/12/15 Rage more: sets the Rage and returns true. Else returns false, and the
    // caller cuts the Rage to `keptTenths` as in stock.
    bool ApplyStanceRage(Unit* warrior, uint32 keptTenths);

    // Spell::EffectDummy, Warrior family: returns true for Spearing Strike (60100, effect 3). With the
    // rules on and a Warrior caster, against a Giant, a Dragonkin or a mounted target, `damage` (the
    // weapon damage of effects 1 and 2) becomes x(1 + effect 3), and a mounted target is dismounted.
    bool SpearingStrike(Unit* caster, Unit* target, SpellEntry const* spellInfo, float& damage);

    // Unit::ProcSkillsAndReactives, for the attacker: a main hand melee attack that lands on a target with
    // the Warrior's own Rend rolls the Bloodthrill chance. Rend itself and Overpower do not roll. On a proc
    // it gives the Overpower combo point and returns the Overpower window in ms (the caller starts
    // REACTIVE_OVERPOWER with it). Else 0.
    uint32 BloodthrillOverpower(Unit* attacker, Unit* target, uint32 procFlag, uint32 procExtra, WeaponAttackType attType,
        SpellEntry const* procSpell);

    // Unit::GetUnitCriticalChance: the Weaponmaster crit chance (percent) of an axe or polearm in the hand
    // of the attack. 0 for other weapons, no talent, or the rules off.
    float WeaponmasterCritBonus(Unit const* attacker, WeaponAttackType attType);

    // SpellCaster::CalcArmorReducedDamage: with a mace or staff in the main hand, Weaponmaster ignores
    // 3-15% of the target's `armor`.
    void WeaponmasterArmor(Unit const* attacker, float& armor);

    // Unit::ProcDamageAndSpellFor, after a proc roll that passed: true when the proc of `auraSpell` must
    // not happen. The Weaponmaster sword proc does not come from Shield Bash or Shield Slam (15 Oct).
    bool WeaponmasterBlocksProc(Unit const* owner, SpellEntry const* auraSpell, SpellEntry const* procSpell);

    // Unit::UpdateMeleeAttackingState and Unit::AttackerStateUpdate: true when the unit swings on while it
    // casts (Improved Slam: the cast of Slam does not stop the swing).
    bool SwingsThroughCast(Unit const* unit);

    // Spell::cast, before it resets the swing timers after a spell with the combat interrupt flag: true
    // when the swing timers stay (Improved Slam: Slam does not reset the swing).
    bool KeepsSwingTimer(Unit const* caster, SpellEntry const* spellInfo);
}

#endif
