/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_FOREVER_WARRIOR_H
#define WOWAI_GAME_FOREVER_WARRIOR_H

#include "Common.h"

class Unit;
struct CleanDamage;
class SpellEntry;

// The Forever Warrior mechanics of jvbastel/WOWAI#214: rage from weapon speed, and crits on the ticks of
// the Warrior's own DoTs (Rend, Deep Wounds). Each function is one hook in an upstream file. Each does
// nothing unless the Forever rules are on and the unit is a Warrior player: other classes, creatures and
// the rules off keep the stock code. The math is in ForeverWarriorRules.h.
namespace Forever
{
    // The Forever rules are on and `unit` is a Warrior player.
    bool IsForeverWarrior(Unit const* unit);

    // Unit::DealDamage, for the rage of direct damage: with IsForeverWarrior(attacker), gives the rage of
    // the weapon speed for a melee swing (cleanDamage: the hand and the crit), none for other damage, and
    // returns true. Else returns false, and the caller gives the stock rage.
    bool RewardSwingRage(Unit* attacker, CleanDamage const* cleanDamage);

    // Unit::DealDamage, rage of a dodged or parried swing: true when the Forever rules give none.
    bool NoRageForAvoidedSwing(Unit const* attacker);

    // Aura::PeriodicTick, periodic damage: a tick of a Warrior's Rend or Deep Wound rolls the melee crit
    // chance. On a crit it changes `damage` to the crit damage and returns true.
    bool RollDotCrit(Unit* caster, Unit* target, SpellEntry const* spellProto, float& damage);

    // Aura::HandlePeriodicDamage at apply: a Warrior's Rend taps the creature at once.
    void TapOnRendApply(Unit* caster, Unit* target, SpellEntry const* spellProto);

    // Spell::EffectDummy, Deep Wounds: the whole bleed damage before the talent percent. With the rules,
    // `damage` becomes the average damage of the weapon itself (no attack power).
    void DeepWoundsWeaponDamage(Unit* caster, float& damage);

    // Spell::EffectDummy, Deep Wounds: the tick damage of the new bleed (`tickDamage`) also gets the
    // damage that the caster's old bleed on `target` had not dealt yet (a rolling bleed).
    void DeepWoundsRoll(Unit* caster, Unit* target, int32& tickDamage);

    // Spell::EffectSchoolDMG: a Forever Warrior's Revenge adds a percent of its attack power to `damage`
    // (ForeverWarriorRules.h). The damage modifiers (Improved Revenge) apply to the sum later.
    void RevengeDamage(Unit* caster, SpellEntry const* spellProto, Unit* target, float& damage);
}

#endif
