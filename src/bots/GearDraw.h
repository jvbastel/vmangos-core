#ifndef WOWAI_BOTS_GEAR_DRAW_H
#define WOWAI_BOTS_GEAR_DRAW_H

#include "GearTemplate.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The gear draw: the items that a character wears from a gear template at a
// level and content patch.
//
// A character draws and wears only the items of its own faction (the
// faction word of an item line, see GearTemplate.h). The items of the other
// faction are left out before anything else, so they never change the draw.
//
// A generated template:
// - For each slot, the pool is the items whose level range holds the level
//   and whose minimum content patch the realm allows, best score first (the
//   lower entry first on equal scores). The draw takes one item from the top
//   GearPoolSize items, weighted toward the top: the item at rank r (0 for
//   the best) of a pool of n weighs n - r. Finger and trinket draw two
//   different items.
// - Weapons: the draw first picks one shape of the template, the same chance
//   for each shape whose slots all have items. Then it draws the items of the
//   slots of that shape only. If no shape has items for all its slots, it
//   takes the shape with the most slots with items, the first in the file on
//   a tie.
// - The seed comes from the character GUID. Each slot and the shape choice
//   have their own random stream, so the same GUID, level, and content patch
//   always give the same gear.
// A hand-made template has no draw: the character wears each item whose
// level range holds the level and whose content patch the realm allows. Where
// two items fill one position, the item of the later content patch wins. A
// position with no item stays empty.
namespace Bots
{
    std::size_t const GearPoolSize = 10;

    struct GearPick
    {
        GearPosition position;
        GearSlot slot;
        std::uint32_t entry;
    };

    struct GearDraw
    {
        std::string shape;            // Empty when no weapon is drawn, and for a hand-made template.
        std::vector<GearPick> picks;  // In the order of GearPosition.
    };

    GearDraw DrawGear(GearTemplate const& gearTemplate, std::uint64_t guid, std::uint32_t level, std::uint32_t patch,
        Faction faction);
}

#endif
