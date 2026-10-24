#include "GearDraw.h"

#include <gtest/gtest.h>

#include <map>
#include <set>

using Bots::DrawGear;
using Bots::GearDraw;
using Bots::GearPosition;
using Bots::GearSlot;
using Bots::GearTemplate;

namespace
{
    GearTemplate Parse(std::string const& text)
    {
        Bots::GearTemplateResult result = Bots::ParseGearTemplate(text);
        EXPECT_TRUE(result.ok) << result.error;
        return result.gearTemplate;
    }

    std::string const Generated = "template warrior_tank warrior tank generated\n";

    std::uint32_t EntryAt(GearDraw const& draw, GearPosition position)
    {
        for (Bots::GearPick const& pick : draw.picks)
            if (pick.position == position)
                return pick.entry;
        return 0;
    }

    // Twelve heads with the scores 1 to 12: entry 100 + score.
    GearTemplate TwelveHeads()
    {
        std::string text = Generated;
        for (int score = 1; score <= 12; ++score)
            text += "item head " + std::to_string(100 + score) + " 10-20 0 " + std::to_string(score) + "\n";
        return Parse(text);
    }

    std::map<std::uint32_t, int> HeadCounts(GearTemplate const& gear, int characters)
    {
        std::map<std::uint32_t, int> counts;
        for (int guid = 1; guid <= characters; ++guid)
            ++counts[EntryAt(DrawGear(gear, guid, 16, 10), GearPosition::Head)];
        return counts;
    }
}

TEST(GearDraw, APoolHoldsOnlyItemsOfTheLevelAndAnAllowedPatch)
{
    GearTemplate gear = Parse(Generated +
        "item head 1 1-15 0 50\n"     // Level too low.
        "item head 2 17-30 0 50\n"    // Level too high.
        "item head 3 10-20 6 50\n"    // Patch 1.8, after the realm's patch.
        "item head 4 16-16 0 1\n");

    for (std::uint64_t guid = 1; guid <= 50; ++guid)
    {
        GearDraw draw = DrawGear(gear, guid, 16, 5);
        ASSERT_EQ(draw.picks.size(), 1u);
        EXPECT_EQ(draw.picks[0].entry, 4u);
        EXPECT_EQ(draw.picks[0].position, GearPosition::Head);
        EXPECT_EQ(draw.picks[0].slot, GearSlot::Head);
    }
    std::set<std::uint32_t> atPatch6;
    for (std::uint64_t guid = 1; guid <= 50; ++guid)
        atPatch6.insert(EntryAt(DrawGear(gear, guid, 16, 6), GearPosition::Head));
    EXPECT_EQ(atPatch6, (std::set<std::uint32_t>{3, 4}));
}

TEST(GearDraw, DrawsOnlyFromTheTop10ByScore)
{
    std::map<std::uint32_t, int> counts = HeadCounts(TwelveHeads(), 3000);

    EXPECT_EQ(counts.count(101), 0u);
    EXPECT_EQ(counts.count(102), 0u);
    for (std::uint32_t entry = 103; entry <= 112; ++entry)
        EXPECT_GT(counts[entry], 0) << entry;
}

TEST(GearDraw, TheDrawLeansTowardTheTop)
{
    // Weights 10 to 1: the best item comes about 10 times as often as the
    // 10th, about 18% of the time.
    std::map<std::uint32_t, int> counts = HeadCounts(TwelveHeads(), 5000);

    EXPECT_GT(counts[112], 4 * counts[103]);
    EXPECT_GT(counts[112], counts[108]);
    EXPECT_GT(counts[108], counts[103]);
    EXPECT_NEAR(counts[112] / 5000.0, 10.0 / 55.0, 0.03);
}

TEST(GearDraw, TheSameCharacterAlwaysGetsTheSameGear)
{
    GearTemplate gear = TwelveHeads();
    std::set<std::uint32_t> heads;
    for (std::uint64_t guid = 1; guid <= 20; ++guid)
    {
        std::uint32_t const head = EntryAt(DrawGear(gear, guid, 16, 10), GearPosition::Head);
        EXPECT_EQ(EntryAt(DrawGear(gear, guid, 16, 10), GearPosition::Head), head);
        heads.insert(head);
    }
    EXPECT_GT(heads.size(), 3u);
}

TEST(GearDraw, FingerAndTrinketTakeTwoDifferentItems)
{
    GearTemplate gear = Parse(Generated +
        "item finger 1 10-20 0 5\nitem finger 2 10-20 0 5\nitem finger 3 10-20 0 5\n"
        "item trinket 7 10-20 0 5\n");

    for (std::uint64_t guid = 1; guid <= 50; ++guid)
    {
        GearDraw draw = DrawGear(gear, guid, 16, 10);
        std::uint32_t const first = EntryAt(draw, GearPosition::Finger1);
        std::uint32_t const second = EntryAt(draw, GearPosition::Finger2);
        EXPECT_NE(first, 0u);
        EXPECT_NE(second, 0u);
        EXPECT_NE(first, second);
        EXPECT_EQ(EntryAt(draw, GearPosition::Trinket1), 7u);
        EXPECT_EQ(EntryAt(draw, GearPosition::Trinket2), 0u);
    }
}

TEST(GearDraw, PicksOneWeaponShapeFirst)
{
    GearTemplate gear = Parse(
        "template mage_ranged mage ranged generated\n"
        "shape staff twohand\n"
        "shape dagger-and-book mainhand holdable\n"
        "item twohand 10 10-20 0 9\n"
        "item mainhand 20 10-20 0 5\n"
        "item holdable 30 10-20 0 4\n"
        "item ranged 40 10-20 0 3\n");

    std::set<std::string> shapes;
    for (std::uint64_t guid = 1; guid <= 100; ++guid)
    {
        GearDraw draw = DrawGear(gear, guid, 16, 10);
        shapes.insert(draw.shape);
        EXPECT_EQ(EntryAt(draw, GearPosition::Ranged), 40u);
        if (draw.shape == "staff")
        {
            EXPECT_EQ(EntryAt(draw, GearPosition::MainHand), 10u);
            EXPECT_EQ(EntryAt(draw, GearPosition::OffHand), 0u);
        }
        else
        {
            EXPECT_EQ(draw.shape, "dagger-and-book");
            EXPECT_EQ(EntryAt(draw, GearPosition::MainHand), 20u);
            EXPECT_EQ(EntryAt(draw, GearPosition::OffHand), 30u);
        }
    }
    EXPECT_EQ(shapes, (std::set<std::string>{"staff", "dagger-and-book"}));
}

TEST(GearDraw, AShapeWithAnEmptySlotIsNotChosen)
{
    GearTemplate gear = Parse(
        "template mage_ranged mage ranged generated\n"
        "shape staff twohand\n"
        "shape dagger-and-book mainhand holdable\n"
        "item twohand 10 10-20 0 9\n"
        "item mainhand 20 10-20 0 5\n"
        "item holdable 30 18-20 0 4\n");   // No book at level 16.

    for (std::uint64_t guid = 1; guid <= 50; ++guid)
        EXPECT_EQ(DrawGear(gear, guid, 16, 10).shape, "staff");
}

TEST(GearDraw, WithNoFullShapeTheMostFilledShapeIsUsed)
{
    GearTemplate gear = Parse(Generated +
        "shape sword-and-board mainhand shield\n"
        "shape axe twohand\n"
        "item mainhand 20 10-20 0 5\n"
        "item shield 30 18-20 0 4\n"
        "item twohand 10 18-20 0 9\n");

    GearDraw draw = DrawGear(gear, 1, 16, 10);
    EXPECT_EQ(draw.shape, "sword-and-board");
    EXPECT_EQ(EntryAt(draw, GearPosition::MainHand), 20u);
    EXPECT_EQ(EntryAt(draw, GearPosition::OffHand), 0u);

    EXPECT_EQ(DrawGear(Parse(Generated), 1, 16, 10).shape, "");
}

TEST(GearDraw, AHandMadeTemplateIsWornAsItIsWithEmptySlots)
{
    GearTemplate gear = Parse(
        "template grukk_dungeon warrior tank hand\n"
        "item chest 1 10-20 0\n"
        "item head 2 1-15 0\n"     // Level too low: the head stays empty.
        "item legs 3 10-20 0\n"
        "item legs 4 10-20 5\n"    // Patch 1.7 replaces the older legs.
        "item finger 5 10-20 0\n"
        "item twohand 6 10-20 0\n");

    for (std::uint64_t guid = 1; guid <= 20; ++guid)
    {
        GearDraw draw = DrawGear(gear, guid, 16, 10);
        EXPECT_EQ(draw.shape, "");
        ASSERT_EQ(draw.picks.size(), 4u);
        EXPECT_EQ(EntryAt(draw, GearPosition::Chest), 1u);
        EXPECT_EQ(EntryAt(draw, GearPosition::Head), 0u);
        EXPECT_EQ(EntryAt(draw, GearPosition::Legs), 4u);
        EXPECT_EQ(EntryAt(draw, GearPosition::Finger1), 5u);
        EXPECT_EQ(EntryAt(draw, GearPosition::Finger2), 0u);
        EXPECT_EQ(EntryAt(draw, GearPosition::MainHand), 6u);
    }
    EXPECT_EQ(EntryAt(DrawGear(gear, 1, 16, 4), GearPosition::Legs), 3u);
}

TEST(GearDraw, PicksComeInPositionOrder)
{
    GearTemplate gear = Parse(Generated +
        "shape axe twohand\n"
        "item twohand 10 10-20 0 9\nitem feet 2 10-20 0 1\nitem head 1 10-20 0 1\n");

    GearDraw draw = DrawGear(gear, 1, 16, 10);
    ASSERT_EQ(draw.picks.size(), 3u);
    EXPECT_EQ(draw.picks[0].position, GearPosition::Head);
    EXPECT_EQ(draw.picks[1].position, GearPosition::Feet);
    EXPECT_EQ(draw.picks[2].position, GearPosition::MainHand);
}
