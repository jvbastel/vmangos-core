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
class Unit;

// The Forever rules of the Warrior (spec jvbastel/WOWAI#204) and of Mining and Blacksmithing (spec #225):
// one realm setting, Forever.Rules, off by default. When it is on, the core loads the talent tables from
// the Forever DBC folder (Forever.DataDir) and the rows of the forever_* world tables on top of the stock
// rows by ID. The stock tables and the stock DBC files never change, so the setting off and a restart
// give vanilla back.
//
// This header is the one facade of the Forever rules for the stock code: a stock file includes only
// Forever.h. Each function is one hook in an upstream file, or a helper that several Forever files share.
// The hooks of the Warrior mechanics and of each talent tree are in the headers that the end of this file
// includes (ForeverWarrior.h, ForeverArms.h, ForeverFury.h, ForeverProtection.h, VictoryRush.h); their
// pure rules are in the *Rules.h headers, and the spell IDs in ForeverSpellIds.h.
namespace Forever
{
    // World::LoadConfigSettings. Reads Forever.Rules and Forever.DataDir at startup; a reload keeps the
    // values of the startup, because the data is loaded only once. Stops the server when the rules are
    // on and Forever.DataDir is empty.
    void LoadConfig(bool reload);

    bool RulesOn();

    // The gate of every Warrior hook: the Forever rules are on and `unit` is a Warrior player. Other
    // classes, creatures, pets and the rules off keep the stock code. (The rules switch at login runs with
    // the rules on or off, so it checks the class itself.)
    bool IsForeverWarrior(Unit const* unit);

    // Player::AddSpell at load, and CharacterDatabaseCleaner: true when the character_spell rows of the
    // unknown spell `spellId` must stay. The stock code deletes the rows of a spell that does not exist for
    // all characters. With the rules off, a Forever-only spell (the free ID range) does not exist, but a
    // Warrior who does not log in while the rules are off must keep it for the day the rules are on again
    // (ForeverSpellIds.h, KeepsRowsOfUnknownSpell).
    bool KeepsRowsOfUnknownSpell(uint32 spellId);

    // Player::_LoadInventory (#233): true when the inventory rows of the unknown item `itemId` must stay. The
    // stock code deletes the rows of an item that does not exist for good. With the rules off, a Forever-only
    // item (the item range of forever/spec/items.tsv) does not exist, but the character keeps it for the day
    // the rules are on again (ForeverProfessionIds.h, KeepsRowsOfUnknownItem). The item is not loaded.
    bool KeepsRowsOfUnknownItem(uint32 itemId);

    // Player::_LoadSpells (#233): true when the known spell `spellId` is not loaded. With the rules on, a stock
    // recipe that the Forever rules remove (#229) is hidden; its character_spell row stays, so the character
    // has it again with the rules off. With the rules off, always false.
    bool HidesKnownSpell(uint32 spellId);

    // LoadDBCStores, for each DBC file that the Forever rules replace (Talent.dbc, TalentTab.dbc): the
    // Forever DBC folder when the rules are on, else the stock folder `stockPath`. A missing file in
    // the Forever folder stops the server like a missing stock file.
    std::string DbcPath(std::string const& stockPath);

    // The world table `table` for the FROM of a loader query: with the rules on, the stock rows whose
    // `key` has no Forever row, plus the rows of forever_<table> without the removal marker (#229), under
    // the name of the stock table. So a marked Forever row removes the stock rows of its key. With the
    // rules off, the stock table.
    std::string Table(char const* table, char const* key);

    // A loader query on the world table `table` whose FROM cannot take Table(), because it names the table
    // twice (ObjectMgr::LoadItemPrototypes: `FROM item_template t1 WHERE patch=(SELECT max(patch) FROM
    // item_template t2 ...)`, #226). `select(name)` gives the query on the table `name` and ends in a WHERE
    // condition. With the rules on: the query on the stock table without the rows whose `key` has a Forever
    // row, UNION ALL the query on forever_<table> without its marked rows (#229). With the rules off: the
    // query on the stock table.
    std::string SelectWithForeverRows(char const* table, char const* key, std::function<std::string(std::string const&)> const& select);

    // SpellMgr::LoadSpells, after the stock rows: with the rules on, loads each forever_spell_template
    // row through `load` (which replaces the stock spell of that ID or adds a new one), after
    // `resize(maxId + 1)` makes room for the highest Forever spell ID. Then reads the baseline spells
    // (LoadBaselineSpells).
    void LoadSpellTemplate(std::function<void(uint32)> const& resize, std::function<void(Field*)> const& load);

    // LoadDBCStores, after the talent tables: with the rules on, keeps the talent spells of the stock
    // Talent.dbc and TalentTab.dbc in `stockDbcPath` (the talent store then holds the Forever trees),
    // so that the rules switch can take vanilla talents away (RulesSwitch.cpp).
    void LoadStockTalents(std::string const& stockDbcPath);

    // WorldSession::HandlePlayerLogin, for a character that was not online, after its spells and action
    // bars are loaded and before they go to the client: the rules switch (RulesSwitch.cpp). The realm
    // stores the rules that each character last logged in under (characters table forever_character).
    // On a login under other rules, a Warrior gets a talent reset and loses the spells that are not in
    // the active rule set, and those spells leave its action bars. Then the character is saved. The
    // profession part needs no switch step and holds for every class (#233): the load keeps the rows of
    // Forever recipes and items with the rules off (KeepsRowsOfUnknownSpell, KeepsRowsOfUnknownItem) and hides
    // a removed stock recipe with the rules on (HidesKnownSpell), and the Warrior switch-off delete skips
    // the recipes.
    // On a Forever login, a Warrior also learns the baseline spells of its level (LearnBaselineSpells),
    // gets stock Charge on its Vanguard Charge buttons without the talent (FixChargeButtons), and gets its
    // shield talent auras once its items are loaded (UpdateShieldTalents).
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
    // At login (OnLogin), once the action bars are loaded: a Forever Warrior without the Vanguard talent
    // gets stock Charge on the buttons of a Vanguard Charge. A talent reset in Player::LoadFromDB runs
    // before the bars are loaded, so OnSpellRemoved cannot convert them there.
    void FixChargeButtons(Player* player, MasterPlayer* master);

    // The baseline spells of the Forever rules (Baseline.cpp, WOWAI#212): the spells that a Warrior learns
    // at their spell level with no trainer visit, for example Victory Rush. The generator lists them in
    // the world table forever_baseline_spell. With the rules on, reads them once at startup.
    void LoadBaselineSpells();

    // With the rules on, teaches a Warrior each baseline spell of its level that it does not know (a
    // disabled one is enabled again). Player::GiveLevel and the login (OnLogin) call it. Returns the
    // number of spells learned.
    uint32 LearnBaselineSpells(Player* player);
}

// The hooks of the Warrior mechanics and of the talent trees (see above).
#include "ForeverWarrior.h"
#include "ForeverArms.h"
#include "ForeverFury.h"
#include "ForeverProtection.h"
#include "VictoryRush.h"

#endif
