/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_FOREVER_VICTORY_RUSH_H
#define WOWAI_GAME_FOREVER_VICTORY_RUSH_H

#include "Common.h"
#include "SpellDefines.h"

class Spell;
class Unit;

// Victory Rush under the Forever rules (jvbastel/WOWAI#213). A Warrior who lands the killing blow on an
// enemy that yields experience or honor gets Victorious (20 sec). While it lasts, the core sets the caster
// aura state that Victory Rush needs (VictoryRushRules.h), in UNIT_FIELD_AURASTATE, so the client shows the
// button usable exactly when the server allows the cast. Each function is one hook in an upstream file and
// does nothing with the rules off: the two spells exist only in the Forever tables.
namespace Forever
{
    // Unit::Kill, for the unit with the killing blow: a Forever Warrior who knows Victory Rush and kills
    // an honor or experience target gets Victorious.
    void OnKillForVictoryRush(Unit* killer, Unit* victim);

    // Aura::HandleAuraDummy (Real apply or remove): Victorious sets or clears the aura state.
    void OnVictoriousAura(Unit* target, uint32 spellId, bool apply);

    // Spell::EffectSchoolDMG: Victory Rush adds the percent of attack power of its effect 3 to the damage.
    void VictoryRushDamage(Spell* spell, SpellEffectIndex effIdx);

    // Spell::EffectDummy: the dummy effects of Victory Rush. Effect 2 heals the caster for its percent of
    // maximum health; effect 3 is the attack power percent (used by VictoryRushDamage). Returns true for
    // a Victory Rush effect (the caller does nothing more), else false.
    bool VictoryRushDummy(Spell* spell, SpellEffectIndex effIdx);
}

#endif
