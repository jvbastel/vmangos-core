/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_FOREVER_PROTECTION_H
#define WOWAI_GAME_FOREVER_PROTECTION_H

#include "Common.h"

class Item;
class Player;
class SpellAuraHolder;
class SpellEntry;
class Unit;

// The Forever Protection talents that need server code (jvbastel/WOWAI#219): Master of Defense, Iron Will,
// Defiance and Bastion. Each function is one hook in an upstream file. Each does nothing unless the Forever
// rules are on and the unit is a Warrior player (Forever::IsForeverWarrior): other classes, creatures and
// the rules off keep the stock code. The pure rules are in ForeverProtectionRules.h.
//
// Defiance and Bastion are passive auras with no item requirement in their spell rows (the 1.12 core
// gates a passive aura on a weapon only). The shield gate is here: the aura is cast only while a usable
// shield is in the off hand, and it goes when the shield comes off or breaks.
namespace Forever
{
    // Spell::CheckCast, for a passive spell (the core casts passives at learn, login and stance change and
    // skips the item checks for them): true when the spell is Defiance or Bastion of a Forever Warrior
    // without a shield. The caller then fails the cast.
    bool ShieldTalentBlocked(Unit const* caster, SpellEntry const* spellInfo);

    // Player::_ApplyItemMods, for the off-hand slot: `item` was equipped (or repaired) with `apply`, or
    // comes off (or breaks) without it. Casts or removes Defiance and Bastion to match.
    void OnOffHandChanged(Player* player, Item const* item, bool apply);

    // WorldSession::HandlePlayerLogin, for a character that was not online: the spells load before the
    // items, so Defiance and Bastion are cast here when the character logs in with a shield.
    void UpdateShieldTalents(Player* player);

    // Unit::ProcSkillsAndReactives, for the victim of a melee or ranged attack: on a dodge or parry with a
    // shield, Master of Defense gives 5 Rage (spell 23602) at its chance.
    void OnDodgeOrParry(Unit* victim, uint32 procExtra);

    // Spell::DoSpellHitOnUnit, after diminishing returns: Iron Will shortens a Charm or Stun effect on the
    // Warrior by its percent. `durationMs` is the duration of the new aura holder.
    void ApplyIronWill(Unit const* target, SpellAuraHolder const* holder, int32& durationMs);
}

#endif
