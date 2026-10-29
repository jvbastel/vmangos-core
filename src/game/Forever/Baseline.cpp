/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// The baseline spells of the Forever rules (spec jvbastel/WOWAI#204, WOWAI#212): a Warrior learns them at
// their spell level with no trainer visit, at a level-up and at a login under the Forever rules.

#include "Forever.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellMgr.h"

#include <memory>
#include <vector>

namespace
{
    // Filled only with the rules on (LoadBaselineSpells).
    std::vector<uint32> baselineSpells;
}

void Forever::LoadBaselineSpells()
{
    baselineSpells.clear();
    if (!RulesOn())
        return;

    // forever_world.sql of forever/generator makes the table. Without it the query fails and logs it.
    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT `spell_id` FROM `forever_baseline_spell`"));
    if (result)
    {
        do
        {
            uint32 const spellId = result->Fetch()[0].GetUInt32();
            if (sSpellMgr.GetSpellEntry(spellId))
                baselineSpells.push_back(spellId);
            else
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "[Forever] Baseline spell %u in forever_baseline_spell does not exist.", spellId);
        } while (result->NextRow());
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Forever] Loaded %u baseline spells.", uint32(baselineSpells.size()));
}

uint32 Forever::LearnBaselineSpells(Player* player)
{
    if (!RulesOn() || player->GetClass() != CLASS_WARRIOR)
        return 0;

    uint32 learned = 0;
    for (uint32 spellId : baselineSpells)
    {
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
        if (!spell || spell->spellLevel > player->GetLevel() || player->HasSpell(spellId))
            continue;
        // Out of the world (at login) it sends nothing: the login sends the whole spellbook after it.
        player->LearnSpell(spellId, false);
        ++learned;
    }
    return learned;
}
