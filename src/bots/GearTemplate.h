#ifndef WOWAI_BOTS_GEAR_TEMPLATE_H
#define WOWAI_BOTS_GEAR_TEMPLATE_H

#include "BotRecipe.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Gear templates: the pool of items that a character of one class and role
// can wear at each level. See "Gear template" in the WOWAI glossary and step 4
// of the version 1 build spec.
//
// FILE FORMAT
//
// One template per file. The game layer reads every `*.txt` file of the
// folder `<Bots.DataDir>/gear/` at startup. In the fork, that folder is
// `bots/data/gear/`. The shared tokenizer of DataTokenizer.h splits the lines:
// words are split by spaces and tabs, `#` starts a comment to the end of the
// line, and blank lines are dropped. The words are in lower case.
//
//   template <name> <class> <role> generated|hand
//   shape <shapeName> <weaponSlot> [<weaponSlot>]       (generated only)
//   item <slot> <entry> <minLevel>-<maxLevel> <minPatch> <score> [horde|alliance]   # Item name
//
// - `template` is the first line, once. The name has only the characters
//   a-z, 0-9, `_`, and `-`, and it is the file name without `.txt`. The class
//   and role are the words of `.wowai bot create`: warrior, priest, rogue,
//   warlock, mage, hunter, shaman, druid; tank, healer, melee, ranged. The
//   class must be able to play the role.
// - A generated template has the name `<class>_<role>`, for example
//   `warrior_tank`, so the file is `warrior_tank.txt`. A hand-made template
//   has any other name, for example `grukk_dungeon.txt`.
// - `item`: `<entry>` is the `item_template` entry. The item can be worn at
//   the character levels `<minLevel>` to `<maxLevel>` (1 to 60, both
//   included). `<minPatch>` is the first content patch of the item, as the
//   core counts it (`WowPatch` in mangosd.conf): 0 = 1.2, 1 = 1.3, 2 = 1.4,
//   3 = 1.5, 4 = 1.6, 5 = 1.7, 6 = 1.8, 7 = 1.9, 8 = 1.10, 9 = 1.11,
//   10 = 1.12. `<score>` is a decimal number above 0, from the stat weights of
//   the generator. A generated item must have a score. A hand-made item has
//   no score: its line ends after `<minPatch>`.
// - The last word of an `item` line may be `horde` or `alliance`: only a
//   character of that faction can use and get the item, and only it draws or
//   wears the item. A line with no faction word is for both factions, so an
//   older template with no faction words is for both factions. The generator
//   writes one template per class and role with the items of both factions.
// - Slots, with the equipment position that each one fills:
//     head neck shoulders back chest shirt tabard wrists hands waist legs
//     feet ranged           one position each (ranged is also the wand)
//     finger trinket        two positions each: two different items
//     mainhand              main hand, a one-handed or main-hand weapon
//     offhand               off hand, a one-handed or off-hand weapon
//     twohand               main hand and off hand, a two-handed weapon
//     shield                off hand, a shield
//     holdable              off hand, an off-hand item that is not a weapon
//   The last five are the weapon slots.
// - `shape`: one weapon shape that the class and role may use, for example
//   `shape staff twohand` or `shape sword-and-board mainhand shield`. Only a
//   generated template has shapes. A generated template with weapon items
//   has at least one shape, and each weapon slot of its items is in a shape.
//   The generator writes only the shapes that the spec allows for the class
//   and role.
// - A hand-made template has at most one item per position at each level
//   and content patch (two for finger and trinket), counted per faction:
//   a Horde item and an Alliance item may share a position. It may leave
//   positions empty.
//
// Examples, with made-up entries:
//
//   template warrior_tank warrior tank generated
//   shape sword-and-board mainhand shield
//   item head 1001 14-20 0 31.5      # A helm
//   item finger 1002 12-18 0 12      # A ring
//   item finger 1003 14-20 3 15.75   # A ring of patch 1.5 and later
//   item finger 1008 14-20 0 14 alliance  # A quest reward of the Alliance only
//   item mainhand 1004 12-18 0 40.25 # A one-handed sword
//   item shield 1005 10-16 0 22      # A shield
//
//   template grukk_dungeon warrior tank hand
//   item chest 1006 16-20 0          # A chest piece
//   item finger 1002 12-18 0         # A ring
//   item twohand 1007 10-20 0        # A two-handed axe
//
// A template's items need not cover every level. A level with no item for a
// slot leaves that slot empty.
namespace Bots
{
    enum class GearSlot
    {
        Head,
        Neck,
        Shoulders,
        Back,
        Chest,
        Shirt,
        Tabard,
        Wrists,
        Hands,
        Waist,
        Legs,
        Feet,
        Finger,
        Trinket,
        Ranged,
        MainHand,
        OffHand,
        TwoHand,
        Shield,
        Holdable,
    };

    // One equipment position of a character. The game layer maps each one to
    // its own equipment slot.
    enum class GearPosition
    {
        Head,
        Neck,
        Shoulders,
        Back,
        Chest,
        Shirt,
        Tabard,
        Wrists,
        Hands,
        Waist,
        Legs,
        Feet,
        Finger1,
        Finger2,
        Trinket1,
        Trinket2,
        Ranged,
        MainHand,
        OffHand,
    };

    enum class GearTemplateKind
    {
        Generated,
        Hand,
    };

    // The faction of a character, for the gear draw.
    enum class GearFaction
    {
        Horde,
        Alliance,
    };

    // The factions that can use and get an item of a template.
    enum class GearFactions
    {
        Both,
        Horde,
        Alliance,
    };

    struct GearItem
    {
        GearSlot slot;
        std::uint32_t entry;
        std::uint32_t minLevel;
        std::uint32_t maxLevel;
        std::uint32_t minPatch;
        double score;      // 0 in a hand-made template.
        std::size_t line;  // The line in the file, for messages.
        GearFactions factions;
    };

    // True when a character of the faction can use and get the item.
    bool FitsFaction(GearItem const& item, GearFaction faction);

    struct WeaponShape
    {
        std::string name;
        std::vector<GearSlot> slots;
    };

    struct GearTemplate
    {
        std::string name;
        BotClass botClass;
        BotRole role;
        GearTemplateKind kind;
        std::vector<WeaponShape> shapes;
        std::vector<GearItem> items;
    };

    struct GearTemplateResult
    {
        bool ok;
        GearTemplate gearTemplate; // Only when ok.
        std::string error;         // "line N: ..." when not ok.
    };

    // Reads the text of one template file. It checks everything that the file
    // can get wrong by itself. The game layer checks that each entry exists.
    GearTemplateResult ParseGearTemplate(std::string const& text);

    // The name that the file of a template must have, without `.txt`. For a
    // generated template it is also `<class>_<role>`. Empty when the file
    // name fits, else a short line for the log.
    std::string CheckGearTemplateFileName(GearTemplate const& gearTemplate, std::string const& fileStem);

    // The name of the generated template of a class and role, for example
    // "warrior_tank".
    std::string GeneratedGearTemplateName(BotClass botClass, BotRole role);

    char const* GearSlotName(GearSlot slot);
    char const* GearPositionName(GearPosition position);
}

#endif
