/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_FOREVER_FURY_H
#define WOWAI_GAME_FOREVER_FURY_H

#include "Common.h"
#include "SharedDefines.h"

class Player;
class SpellEntry;
class Unit;
struct CleanDamage;

// The Forever Fury talents of jvbastel/WOWAI#218 that need server code: Lingering Rage, Furious
// Precision, Blood Craze, Improved Berserker Rage and Gore Drinker. (The Bloodthirst talent is the
// Bloodthirst script of #212 plus data.) Each function is one hook in an upstream file. Each does nothing
// unless the Forever rules are on and the unit is a Warrior player (Forever::IsForeverWarrior): other
// classes, creatures and the rules off keep the stock code. The pure rules are in ForeverFuryRules.h.
namespace Forever
{
    // Unit::Update, when a player drops out of combat: a Warrior with Lingering Rage and rage gets the
    // Lingering Rage buff for the talent's delay (2-10 sec).
    void OnLeaveCombat(Unit* unit);

    // Player::RegenerateAll, out of combat: false while the Lingering Rage buff runs (no rage decay).
    bool RageDecays(Player const* player);

    // SpellCaster::GetMeleeMissChance: the hit chance (percent) that Furious Precision adds to an
    // off-hand attack of `attacker`.
    float OffHandHitBonus(Unit const* attacker, WeaponAttackType attType);

    // Spell::cast, after the effects: Gore Drinker (Enrage, Berserker Rage, Bloodrage, Death Wish or
    // Bloodthirst gives the Gore Drinker buff) and Improved Berserker Rage (Berserker Rage may remove the
    // snares and roots: the harmful slow and root auras).
    void OnSpellCast(Unit* caster, SpellEntry const* spellInfo);

    // Unit::DealDamage, after the rage of the attacker: a melee attack (a swing or a melee ability) that
    // dealt damage uses a charge of the attacker's Gore Drinker buff and heals 0.5% / 1% of its maximum
    // health.
    void OnMeleeDamageDealt(Unit* attacker, Unit* victim, DamageEffectType damagetype,
        CleanDamage const* cleanDamage, SpellEntry const* spellProto);

    // Unit::DealDamage, a living player victim: a single attack (not a DoT tick, not environmental
    // damage) of more than 20% of the victim's maximum health triggers its Blood Craze heal.
    void OnDamageTaken(Unit* attacker, Unit* victim, uint32 damage, DamageEffectType damagetype);
}

#endif
