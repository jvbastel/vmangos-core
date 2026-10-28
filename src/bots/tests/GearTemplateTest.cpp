#include "GearTemplate.h"

#include <gtest/gtest.h>

using Bots::BotClass;
using Bots::BotRole;
using Bots::GearSlot;
using Bots::GearTemplateKind;
using Bots::GearTemplateResult;
using Bots::ParseGearTemplate;

TEST(GearTemplate, ReadsAGeneratedTemplate)
{
    GearTemplateResult result = ParseGearTemplate(
        "# Made by the generator\n"
        "template warrior_tank warrior tank generated\n"
        "shape sword-and-board mainhand shield\n"
        "item head 10399 14-20 0 31.5   # Blackened Defias Belt\n"
        "item mainhand 2024 12-18 3 40.25\n"
        "item shield 2129 10-16 0 22\n");

    ASSERT_TRUE(result.ok) << result.error;
    Bots::GearTemplate const& gear = result.gearTemplate;
    EXPECT_EQ(gear.name, "warrior_tank");
    EXPECT_EQ(gear.botClass, BotClass::Warrior);
    EXPECT_EQ(gear.role, BotRole::Tank);
    EXPECT_EQ(gear.kind, GearTemplateKind::Generated);
    ASSERT_EQ(gear.shapes.size(), 1u);
    EXPECT_EQ(gear.shapes[0].name, "sword-and-board");
    EXPECT_EQ(gear.shapes[0].slots, (std::vector<GearSlot>{GearSlot::MainHand, GearSlot::Shield}));
    ASSERT_EQ(gear.items.size(), 3u);
    Bots::GearItem const& head = gear.items[0];
    EXPECT_EQ(head.slot, GearSlot::Head);
    EXPECT_EQ(head.entry, 10399u);
    EXPECT_EQ(head.minLevel, 14u);
    EXPECT_EQ(head.maxLevel, 20u);
    EXPECT_EQ(head.minPatch, 0u);
    EXPECT_DOUBLE_EQ(head.score, 31.5);
    EXPECT_EQ(head.line, 4u);
    EXPECT_EQ(gear.items[1].minPatch, 3u);
}

TEST(GearTemplate, ReadsAHandMadeTemplateWithoutScores)
{
    GearTemplateResult result = ParseGearTemplate(
        "template grukk_dungeon warrior tank hand\n"
        "item chest 2870 16-16 0\n"
        "item finger 1491 10-20 0\n"
        "item finger 1492 10-20 0\n");

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.gearTemplate.kind, GearTemplateKind::Hand);
    ASSERT_EQ(result.gearTemplate.items.size(), 3u);
    EXPECT_EQ(result.gearTemplate.items[0].slot, GearSlot::Chest);
    EXPECT_DOUBLE_EQ(result.gearTemplate.items[0].score, 0.0);
}

namespace
{
    std::string ErrorOf(std::string const& text)
    {
        GearTemplateResult result = ParseGearTemplate(text);
        EXPECT_FALSE(result.ok);
        return result.error;
    }

    std::string const TankHeader = "template warrior_tank warrior tank generated\n";
    std::string const HandHeader = "template grukk warrior tank hand\n";
}

TEST(GearTemplate, RefusesABadTemplateLine)
{
    EXPECT_EQ(ErrorOf(""), "line 1: The file has no template line.");
    EXPECT_EQ(ErrorOf("item head 1 1-10 0 5\n"), "line 1: The first line must be the template line.");
    EXPECT_EQ(ErrorOf("template a warrior tank\n"),
        "line 1: Use: template <name> <class> <role> generated|hand");
    EXPECT_EQ(ErrorOf("template Big! warrior tank hand\n"),
        "line 1: A template name has only a-z, 0-9, _ and -.");
    EXPECT_EQ(ErrorOf("template a paladin tank hand\n"), "line 1: Unknown Horde class 'paladin'.");
    EXPECT_EQ(ErrorOf("template a warrior dps hand\n"), "line 1: Unknown role 'dps'.");
    EXPECT_EQ(ErrorOf("template a rogue tank hand\n"), "line 1: A rogue cannot play the tank role.");
    EXPECT_EQ(ErrorOf("template a warrior tank made\n"), "line 1: The kind must be generated or hand, not 'made'.");
    EXPECT_EQ(ErrorOf(TankHeader + "\n" + TankHeader), "line 3: The file has two template lines.");
    EXPECT_EQ(ErrorOf(TankHeader + "weapon mainhand\n"), "line 2: Unknown line 'weapon'. Use template, shape, or item.");
}

TEST(GearTemplate, RefusesABadItemLine)
{
    EXPECT_EQ(ErrorOf(TankHeader + "item head 1 1-10\n"),
        "line 2: Use: item <slot> <entry> <minLevel>-<maxLevel> <minPatch> <score> [horde|alliance]");
    EXPECT_EQ(ErrorOf(TankHeader + "item hat 1 1-10 0 5\n"), "line 2: Unknown slot 'hat'.");
    EXPECT_EQ(ErrorOf(TankHeader + "item head x1 1-10 0 5\n"), "line 2: The entry 'x1' is not a number above 0.");
    EXPECT_EQ(ErrorOf(TankHeader + "item head 0 1-10 0 5\n"), "line 2: The entry '0' is not a number above 0.");
    EXPECT_EQ(ErrorOf(TankHeader + "item head 1 10 0 5\n"), "line 2: The levels '10' are not <min>-<max> from 1 to 60.");
    EXPECT_EQ(ErrorOf(TankHeader + "item head 1 12-10 0 5\n"), "line 2: The levels '12-10' are not <min>-<max> from 1 to 60.");
    EXPECT_EQ(ErrorOf(TankHeader + "item head 1 0-10 0 5\n"), "line 2: The levels '0-10' are not <min>-<max> from 1 to 60.");
    EXPECT_EQ(ErrorOf(TankHeader + "item head 1 50-61 0 5\n"), "line 2: The levels '50-61' are not <min>-<max> from 1 to 60.");
    EXPECT_EQ(ErrorOf(TankHeader + "item head 1 1-10 11 5\n"), "line 2: The content patch '11' is not a number from 0 to 10.");
    EXPECT_EQ(ErrorOf(TankHeader + "item head 1 1-10 0 zero\n"), "line 2: The score 'zero' is not a number above 0.");
    EXPECT_EQ(ErrorOf(TankHeader + "item head 1 1-10 0 0\n"), "line 2: The score '0' is not a number above 0.");
    EXPECT_EQ(ErrorOf(HandHeader + "item head 1 1-10 0 5\n"),
        "line 2: Use: item <slot> <entry> <minLevel>-<maxLevel> <minPatch> [horde|alliance] (a hand-made item has no score)");
}

TEST(GearTemplate, RefusesBadWeaponShapes)
{
    EXPECT_EQ(ErrorOf(TankHeader + "shape board\n"), "line 2: Use: shape <name> <weaponSlot> [<weaponSlot>]");
    EXPECT_EQ(ErrorOf(TankHeader + "shape board mainhand head\n"),
        "line 2: 'head' is not a weapon slot. Use mainhand, offhand, twohand, shield, or holdable.");
    EXPECT_EQ(ErrorOf(TankHeader + "shape board twohand shield\n"),
        "line 2: The slots of a shape fill the main hand and the off hand once each.");
    EXPECT_EQ(ErrorOf(TankHeader + "shape board mainhand\nshape board twohand\n"),
        "line 3: Two shapes are named 'board'.");
    EXPECT_EQ(ErrorOf(HandHeader + "shape board mainhand shield\n"),
        "line 2: A hand-made template has no shapes: it has no draw.");
    EXPECT_EQ(ErrorOf(TankHeader + "shape board mainhand shield\nitem twohand 1 1-10 0 5\n"),
        "line 3: The slot twohand is in no shape line.");
}

TEST(GearTemplate, AHandMadeTemplateFillsEachPositionOnce)
{
    // One item per position at each level and content patch, two for finger
    // and trinket. A two-handed weapon fills both hands.
    EXPECT_EQ(ErrorOf(HandHeader + "item head 1 10-20 0\nitem head 2 20-30 0\n"),
        "line 3: Two items fill the position head at level 20 and content patch 0.");
    EXPECT_EQ(ErrorOf(HandHeader + "item finger 1 10-20 0\nitem finger 2 10-20 0\nitem finger 3 15-15 0\n"),
        "line 4: Three items fill the position finger at level 15 and content patch 0.");
    EXPECT_EQ(ErrorOf(HandHeader + "item twohand 1 10-20 0\nitem shield 2 12-14 0\n"),
        "line 3: Two items fill the position offhand at level 12 and content patch 0.");
    EXPECT_TRUE(ParseGearTemplate(HandHeader + "item head 1 10-19 0\nitem head 2 20-30 0\nitem head 3 10-30 5\n").ok);
}

TEST(GearTemplate, TheFileNameIsTheTemplateName)
{
    Bots::GearTemplate gear = ParseGearTemplate(TankHeader).gearTemplate;
    EXPECT_EQ(Bots::CheckGearTemplateFileName(gear, "warrior_tank"), "");
    EXPECT_EQ(Bots::CheckGearTemplateFileName(gear, "tank"),
        "The file of the template warrior_tank must be warrior_tank.txt.");

    gear = ParseGearTemplate("template warrior_melee warrior tank generated\n").gearTemplate;
    EXPECT_EQ(Bots::CheckGearTemplateFileName(gear, "warrior_melee"),
        "A generated template of a warrior tank must be named warrior_tank.");

    gear = ParseGearTemplate("template warrior_tank warrior tank hand\n").gearTemplate;
    EXPECT_EQ(Bots::CheckGearTemplateFileName(gear, "warrior_tank"),
        "The name warrior_tank is for the generated template. Give the hand-made template another name.");

    EXPECT_EQ(Bots::GeneratedGearTemplateName(BotClass::Mage, BotRole::Ranged), "mage_ranged");
}

TEST(GearTemplate, TwoRingsThatNeverMeetLeaveRoomForAThird)
{
    EXPECT_TRUE(ParseGearTemplate(HandHeader +
        "item finger 1 10-12 0\nitem finger 2 18-20 0\nitem finger 3 10-20 0\n").ok);
}

// Ticket #190: an item line may end with the faction that can use and get it.
TEST(GearTemplate, AnItemLineCanBeMarkedForOneFaction)
{
    GearTemplateResult result = ParseGearTemplate(TankHeader +
        "item head 1 10-20 0 5 horde   # Orc helm\n"
        "item head 2 10-20 0 4 alliance\n"
        "item head 3 10-20 0 3\n");

    ASSERT_TRUE(result.ok) << result.error;
    std::vector<Bots::GearItem> const& items = result.gearTemplate.items;
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].factions, Bots::GearFactions::Horde);
    EXPECT_EQ(items[1].factions, Bots::GearFactions::Alliance);
    EXPECT_EQ(items[2].factions, Bots::GearFactions::Both);
    EXPECT_TRUE(Bots::FitsFaction(items[0], Bots::GearFaction::Horde));
    EXPECT_FALSE(Bots::FitsFaction(items[0], Bots::GearFaction::Alliance));
    EXPECT_FALSE(Bots::FitsFaction(items[1], Bots::GearFaction::Horde));
    EXPECT_TRUE(Bots::FitsFaction(items[1], Bots::GearFaction::Alliance));
    EXPECT_TRUE(Bots::FitsFaction(items[2], Bots::GearFaction::Horde));
    EXPECT_TRUE(Bots::FitsFaction(items[2], Bots::GearFaction::Alliance));
}

TEST(GearTemplate, AHandMadeItemCanBeMarkedForOneFaction)
{
    GearTemplateResult result = ParseGearTemplate(HandHeader + "item head 1 10-20 0 alliance\nitem chest 2 10-20 0\n");

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.gearTemplate.items[0].factions, Bots::GearFactions::Alliance);
    EXPECT_EQ(result.gearTemplate.items[1].factions, Bots::GearFactions::Both);
}

TEST(GearTemplate, TheFactionWordIsHordeOrAlliance)
{
    EXPECT_EQ(ErrorOf(TankHeader + "item head 1 10-20 0 5 both\n"),
        "line 2: The faction 'both' is not horde or alliance. A line for both factions has no faction word.");
    EXPECT_EQ(ErrorOf(HandHeader + "item head 1 10-20 0 orcs\n"),
        "line 2: The faction 'orcs' is not horde or alliance. A line for both factions has no faction word.");
}

TEST(GearTemplate, AHordeItemAndAnAllianceItemCanFillOnePosition)
{
    EXPECT_TRUE(ParseGearTemplate(HandHeader + "item head 1 10-20 0 horde\nitem head 2 10-20 0 alliance\n").ok);
    EXPECT_EQ(ErrorOf(HandHeader + "item head 1 10-20 0 horde\nitem head 2 10-20 0\n"),
        "line 3: Two items fill the position head at level 10 and content patch 0.");
}

// jvbastel/WOWAI#190 (merge review): the position count is per faction.
TEST(GearTemplate, ALineForBothFactionsFillsThePositionOfEachFaction)
{
    std::string const rings = "item finger 1 10-20 0 horde\nitem finger 2 10-20 0 alliance\nitem finger 3 10-20 0\n";

    EXPECT_TRUE(ParseGearTemplate(HandHeader + rings).ok);
    EXPECT_EQ(ErrorOf(HandHeader + rings + "item finger 4 10-20 0 horde\n"),
        "line 5: Three items fill the position finger at level 10 and content patch 0.");
}
