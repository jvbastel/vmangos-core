/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "Forever.h"
#include "ForeverProfessionIds.h"
#include "ForeverSpellIds.h"
#include "Config/Config.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Progression.h"
#include "SharedDefines.h"
#include "Unit.h"

#include <cstdlib>
#include <memory>
#include <unordered_set>

namespace
{
    bool rulesOn = false;
    std::string dataDir;
    // The stock recipes that the Forever rules remove (#229), filled with the rules on (LoadRemovedRecipes).
    std::unordered_set<uint32> removedRecipes;

    // The removal marker of the override tables (#229, forever/generator/forever_gen/removals.py): a
    // forever_<table> row with this column 1 takes the stock rows of its key away and does not load itself.
    char const* const RemovalMarker = "forever_removed";

    // The columns of the world table `table` in their order, as "`a`, `b`, ...".
    std::string ColumnList(char const* table)
    {
        std::string columns;
        std::unique_ptr<QueryResult> result(WorldDatabase.PQuery("SELECT `COLUMN_NAME` FROM `information_schema`.`COLUMNS` WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = '%s' ORDER BY `ORDINAL_POSITION`", table));
        if (result)
        {
            do
            {
                if (!columns.empty())
                    columns += ", ";
                columns += "`" + result->Fetch()[0].GetCppString() + "`";
            } while (result->NextRow());
        }
        return columns;
    }

    // Whether forever_<table> has the removal marker column. forever_world.sql of forever/generator adds it
    // to each override table; a table without it (an older forever_world.sql) has no removal rows.
    bool HasRemovalMarker(std::string const& foreverTable)
    {
        std::unique_ptr<QueryResult> found(WorldDatabase.PQuery("SELECT 1 FROM `information_schema`.`COLUMNS` WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = '%s' AND `COLUMN_NAME` = '%s'", foreverTable.c_str(), RemovalMarker));
        return bool(found);
    }

    // The recipe spells whose skill line abilities forever_skill_line_ability marks removed (#229), and which
    // no unmarked Forever row teaches. Their character_spell rows are not loaded with the rules on (#233).
    void LoadRemovedRecipes()
    {
        removedRecipes.clear();
        if (!HasRemovalMarker("forever_skill_line_ability"))
            return;
        std::unique_ptr<QueryResult> result(WorldDatabase.PQuery("SELECT DISTINCT `spell_id` FROM `forever_skill_line_ability` WHERE `%s` = 1 AND `spell_id` NOT IN (SELECT `spell_id` FROM `forever_skill_line_ability` WHERE `%s` = 0)", RemovalMarker, RemovalMarker));
        if (result)
        {
            do
                removedRecipes.insert(result->Fetch()[0].GetUInt32());
            while (result->NextRow());
        }
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Forever] %u removed stock recipes are not loaded for the characters who know them.", uint32(removedRecipes.size()));
    }
}

void Forever::LoadConfig(bool reload)
{
    bool const rules = sConfig.GetBoolDefault("Forever.Rules", false);
    std::string dir = sConfig.GetStringDefault("Forever.DataDir", "");
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
        dir += '/';

    if (reload)
    {
        if (rules != rulesOn || dir != dataDir)
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Forever] Forever.Rules and Forever.DataDir change only at a restart. Keeping Rules %u, DataDir \"%s\".",
                uint32(rulesOn), dataDir.c_str());
        return;
    }

    rulesOn = rules;
    dataDir = dir;
    if (rulesOn && dataDir.empty())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Forever] Forever.Rules = 1 needs Forever.DataDir, the Forever DBC folder of forever/generator.");
        Log::WaitBeforeContinueIfNeed();
        exit(1);
    }
    if (rulesOn)
    {
        // Without one of the forever_* tables, the union of Table() fails and the stock table loads 0 rows
        // (spell IDs, chains, proc events, skill lines, threat, items, trainers, gameobject loot). Stop before
        // any of them loads.
        static char const* const foreverTables[] = { "forever_spell_template", "forever_spell_chain",
            "forever_spell_proc_event", "forever_skill_line_ability", "forever_spell_threat", "forever_baseline_spell",
            "forever_item_template", "forever_npc_trainer", "forever_gameobject_loot_template" };
        for (char const* table : foreverTables)
        {
            std::unique_ptr<QueryResult> found(WorldDatabase.PQuery("SELECT 1 FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = '%s'", table));
            if (!found)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Forever] The Forever rules are on, but the world table %s is missing. Apply the world migrations and load forever/generator's forever_world.sql.", table);
                Log::WaitBeforeContinueIfNeed();
                exit(1);
            }
        }
    }
    if (rulesOn)
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Forever] The Forever rules are on. DataDir %s", dataDir.c_str());
    else
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Forever] The Forever rules are off.");
}

bool Forever::RulesOn()
{
    return rulesOn;
}

bool Forever::IsForeverWarrior(Unit const* unit)
{
    return rulesOn && unit && unit->IsPlayer() && unit->GetClass() == CLASS_WARRIOR;
}

bool Forever::KeepsRowsOfUnknownSpell(uint32 spellId)
{
    bool const keep = SpellIds::KeepsRowsOfUnknownSpell(spellId, rulesOn);
    if (keep)
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Spell %u exists only under the Forever rules: its character_spell rows stay.", spellId);
    return keep;
}

bool Forever::KeepsRowsOfUnknownItem(uint32 itemId)
{
    bool const keep = ProfessionIds::KeepsRowsOfUnknownItem(itemId, rulesOn);
    if (keep)
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Forever] Item %u exists only under the Forever rules: it is not loaded, and its rows stay.", itemId);
    return keep;
}

bool Forever::IsHiddenRecipe(uint32 spellId)
{
    return ProfessionIds::HidesKnownSpell(removedRecipes.count(spellId) != 0, rulesOn);
}

bool Forever::HidesKnownSpell(uint32 spellId)
{
    bool const hide = IsHiddenRecipe(spellId);
    if (hide)
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Forever] Recipe %u is removed under the Forever rules: it is not loaded, and its row stays.", spellId);
    return hide;
}

std::string Forever::DbcPath(std::string const& stockPath)
{
    return rulesOn ? dataDir : stockPath;
}

std::string Forever::Table(char const* table, char const* key)
{
    if (!rulesOn)
        return std::string("`") + table + "`";
    std::string const stock = std::string("`") + table + "`";
    std::string const forever = std::string("`forever_") + table + "`";
    std::string const column = std::string("`") + key + "`";
    // A marked row still hides the stock rows of its key (NOT IN), but does not load itself. The loaders
    // read the row by position, so the forever rows give the stock columns only, not the marker.
    std::string foreverRows = "SELECT * FROM " + forever;
    if (HasRemovalMarker(std::string("forever_") + table))
        foreverRows = "SELECT " + ColumnList(table) + " FROM " + forever + " WHERE `" + RemovalMarker + "` = 0";
    return "(SELECT * FROM " + stock + " WHERE " + column + " NOT IN (SELECT " + column + " FROM " + forever +
        ") UNION ALL " + foreverRows + ") AS " + stock;
}

std::string Forever::SelectWithForeverRows(char const* table, char const* key, std::function<std::string(std::string const&)> const& select)
{
    std::string const stock = std::string("`") + table + "`";
    if (!rulesOn)
        return select(stock);
    std::string const forever = std::string("`forever_") + table + "`";
    std::string const column = std::string("`") + key + "`";
    // `select` names its columns, so the marker column is not read. A marked row (#229) does not load.
    std::string foreverRows = select(forever);
    if (HasRemovalMarker(std::string("forever_") + table))
        foreverRows += std::string(" AND `") + RemovalMarker + "` = 0";
    return select(stock) + " AND " + column + " NOT IN (SELECT " + column + " FROM " + forever + ") UNION ALL " + foreverRows;
}

std::set<uint32> Forever::ForeverRowKeys(char const* table, char const* key)
{
    std::set<uint32> keys;
    if (!rulesOn)
        return keys;
    std::unique_ptr<QueryResult> result(WorldDatabase.PQuery("SELECT DISTINCT `%s` FROM `forever_%s`", key, table));
    if (result)
    {
        do
            keys.insert(result->Fetch()[0].GetUInt32());
        while (result->NextRow());
    }
    return keys;
}

void Forever::LoadSpellTemplate(std::function<void(uint32)> const& resize, std::function<void(Field*)> const& load)
{
    if (!rulesOn)
        return;

    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT MAX(`entry`) FROM `forever_spell_template`"));
    uint32 const maxId = result ? result->Fetch()[0].GetUInt32() : 0;
    if (!maxId)
    {
        // Review S2 of #204: without the Forever data the realm would run vanilla spells under Forever trees.
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Forever] The Forever rules are on, but forever_spell_template is empty. Load forever/generator's forever_world.sql.");
        Log::WaitBeforeContinueIfNeed();
        exit(1);
    }
    resize(maxId + 1);

    // Read by position with SELECT *: forever_spell_template has no removal marker column (a spell cannot be
    // removed, forever/generator/forever_gen/removals.py NOT_REMOVABLE), so its rows have the stock columns only.
    result = WorldDatabase.PQuery("SELECT * FROM `forever_spell_template` t1 WHERE `build`=(SELECT MAX(`build`) FROM `forever_spell_template` t2 WHERE t1.`entry`=t2.`entry` && `build` <= %u)", SUPPORTED_CLIENT_BUILD);
    uint32 count = 0;
    if (result)
    {
        do
        {
            load(result->Fetch());
            ++count;
        } while (result->NextRow());
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Forever] Loaded %u Forever spell rows on top of the stock spells.", count);

    LoadBaselineSpells();
    LoadRemovedRecipes();
}
