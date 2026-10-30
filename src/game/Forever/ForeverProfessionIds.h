/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_FOREVER_PROFESSION_IDS_H
#define WOWAI_GAME_FOREVER_PROFESSION_IDS_H

// The ID ranges of the Forever professions (spec jvbastel/WOWAI#225) that the rules switch knows by number
// (#233): with the rules off the core loads no Forever table, so only the ID tells a Forever recipe or item
// apart. The spec files hold the source of truth: the "# Free profession spell ID range" line of
// forever/spec/blacksmithing.tsv and the "# Free item ID range" line of forever/spec/items.tsv.
// forever/generator/tests/test_profession_ids.py fails when a constant and its spec line differ.
//
// No game header, so wowai/tests/forever-rules.test.sh tests it on the host.

#include "ForeverSpellIds.h"

#include <cstdint>

namespace Forever
{
namespace ProfessionIds
{
    // The profession spells (recipes): a block of the free spell range (ForeverSpellIds.h).
    constexpr uint32_t ProfessionSpellFirst = 61000;
    constexpr uint32_t ProfessionSpellLast = 61999;
    // The Forever-only items. The stock 1.12 world database ends at item 25818.
    constexpr uint32_t ItemIdFirst = 70000;
    constexpr uint32_t ItemIdLast = 79999;

    constexpr bool IsProfessionSpell(uint32_t spellId)
    {
        return spellId >= ProfessionSpellFirst && spellId <= ProfessionSpellLast;
    }

    constexpr bool IsForeverOnlyItem(uint32_t itemId)
    {
        return itemId >= ItemIdFirst && itemId <= ItemIdLast;
    }

    // The switch of a Warrior to the vanilla rules deletes the rows of its Forever-only spells (talents,
    // abilities), but a recipe is not a Warrior spell: it stays for the next Forever login, as for any class.
    constexpr bool WarriorSwitchOffDeletes(uint32_t spellId)
    {
        return SpellIds::IsForeverOnlySpell(spellId) && !IsProfessionSpell(spellId);
    }

    // At load, the stock core deletes the inventory rows of an item that does not exist, for good
    // (Player::_LoadInventory). With the rules off, a Forever-only item does not exist; its rows stay and the
    // item is not loaded, so that it is back when the rules are on again. With the rules on, an unknown
    // Forever item is one that the spec dropped, and the stock cleanup goes on.
    constexpr bool KeepsRowsOfUnknownItem(uint32_t itemId, bool rulesOn)
    {
        return !rulesOn && IsForeverOnlyItem(itemId);
    }

    // At load: a stock recipe that the Forever rules remove (#229, a marked skill line ability) is not loaded
    // with the rules on. Its character_spell row stays (Player::_SaveSpells touches only loaded spells), so
    // the recipe is back with the rules off.
    constexpr bool HidesKnownSpell(bool removedRecipe, bool rulesOn)
    {
        return rulesOn && removedRecipe;
    }
}
}

#endif
