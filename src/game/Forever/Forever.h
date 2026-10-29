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
class MasterPlayer;
class Player;

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

    // LoadDBCStores, after the talent tables: with the rules on, keeps the talent spells of the stock
    // Talent.dbc and TalentTab.dbc in `stockDbcPath` (the talent store then holds the Forever trees),
    // so that the rules switch can take vanilla talents away (RulesSwitch.cpp).
    void LoadStockTalents(std::string const& stockDbcPath);

    // WorldSession::HandlePlayerLogin, for a character that was not online, after its spells and action
    // bars are loaded and before they go to the client: the rules switch (RulesSwitch.cpp). The realm
    // stores the rules that each character last logged in under (characters table forever_character).
    // On a login under other rules, a Warrior gets a talent reset and loses the spells that are not in
    // the active rule set, and those spells leave its action bars. Then the character is saved.
    void OnLogin(Player* player, MasterPlayer* master);

    // Vanguard (Vanguard.cpp, WOWAI#216): the talent swaps Charge for Vanguard Charge, which the client
    // allows in Defensive Stance, in the spellbook and on the action bars.
    // Player::LearnSpell, at its end, for a spell that is not learned as a dependent of another: after
    // the Vanguard talent or a stock Charge rank, with the rules on, a Warrior with the talent gets the
    // Vanguard Charge of its highest stock Charge rank in place of Charge.
    void OnSpellLearned(Player* player, uint32 spellId);
    // Player::RemoveSpell, at its end: when a Warrior loses the Vanguard talent (a talent reset), stock
    // Charge comes back on the same buttons.
    void OnSpellRemoved(Player* player, uint32 spellId);
    // The rules switch to vanilla (RulesSwitch.cpp), at login: the highest stock Charge rank comes back
    // in the spellbook and on the buttons of the Vanguard Charge that the vanilla core no longer knows.
    void GiveBackStockCharge(Player* player);
}

#endif
