/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "Forever.h"
#include "Config/Config.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Progression.h"

#include <cstdlib>
#include <memory>

namespace
{
    bool rulesOn = false;
    std::string dataDir;
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
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Forever] The Forever rules are on. DataDir %s", dataDir.c_str());
    else
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Forever] The Forever rules are off.");
}

bool Forever::RulesOn()
{
    return rulesOn;
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
    return "(SELECT * FROM " + stock + " WHERE " + column + " NOT IN (SELECT " + column + " FROM " + forever +
        ") UNION ALL SELECT * FROM " + forever + ") AS " + stock;
}

void Forever::LoadSpellTemplate(std::function<void(uint32)> const& resize, std::function<void(Field*)> const& load)
{
    if (!rulesOn)
        return;

    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT MAX(`entry`) FROM `forever_spell_template`"));
    uint32 const maxId = result ? result->Fetch()[0].GetUInt32() : 0;
    if (!maxId)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Forever] The Forever rules are on, but forever_spell_template is empty. Load forever/generator's forever_world.sql.");
        return;
    }
    resize(maxId + 1);

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
}
