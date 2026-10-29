/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_FOREVER_H
#define WOWAI_GAME_FOREVER_H

#include "Common.h"

#include <functional>
#include <string>

class Field;

// The Forever rules of the Warrior (spec jvbastel/WOWAI#204): one realm setting, Forever.Rules, off by
// default. When it is on, the core loads the talent tables from the Forever DBC folder (Forever.DataDir)
// and the rows of the forever_* world tables on top of the stock rows by ID. The stock tables and the
// stock DBC files never change, so the setting off and a restart give vanilla back. Each function is
// one hook in an upstream file.
namespace Forever
{
    // World::LoadConfigSettings. Reads Forever.Rules and Forever.DataDir at startup; a reload keeps the
    // values of the startup, because the data is loaded only once. Stops the server when the rules are
    // on and Forever.DataDir is empty.
    void LoadConfig(bool reload);

    bool RulesOn();

    // LoadDBCStores, for each DBC file that the Forever rules replace (Talent.dbc, TalentTab.dbc): the
    // Forever DBC folder when the rules are on, else the stock folder `stockPath`. A missing file in
    // the Forever folder stops the server like a missing stock file.
    std::string DbcPath(std::string const& stockPath);

    // The world table `table` for the FROM of a loader query: with the rules on, the stock rows whose
    // `key` has no Forever row, plus all rows of forever_<table>, under the name of the stock table.
    // With the rules off, the stock table.
    std::string Table(char const* table, char const* key);

    // SpellMgr::LoadSpells, after the stock rows: with the rules on, loads each forever_spell_template
    // row through `load` (which replaces the stock spell of that ID or adds a new one), after
    // `resize(maxId + 1)` makes room for the highest Forever spell ID.
    void LoadSpellTemplate(std::function<void(uint32)> const& resize, std::function<void(Field*)> const& load);
}

#endif
