#include "Loot.h"

#include <gtest/gtest.h>

using Bots::ChooseLootRoll;
using Bots::LootRoll;

TEST(LootRoll, ABotNeverRollsNeedOnAnItemThatItsClassCannotUse)
{
    EXPECT_EQ(ChooseLootRoll(false, true), LootRoll::Greed);
}

TEST(LootRoll, ABotRollsNeedOnlyOnAClearUpgradeThatItCanUse)
{
    EXPECT_EQ(ChooseLootRoll(true, true), LootRoll::Need);
    EXPECT_EQ(ChooseLootRoll(true, false), LootRoll::Greed);
    EXPECT_EQ(ChooseLootRoll(false, false), LootRoll::Greed);
}

namespace
{
    using Bots::GearOption;
    using Bots::GearScores;
    using Bots::ItemGrade;

    GearScores Scores(std::string const& items)
    {
        Bots::GearTemplateResult result = Bots::ParseGearTemplate(
            "template priest_healer priest healer generated\n"
            "shape staff twohand\n"
            "shape mace-and-off-hand mainhand holdable\n" + items);
        EXPECT_TRUE(result.ok) << result.error;
        return GearScores(result.gearTemplate);
    }

    GearOption EmptyPosition()
    {
        return GearOption();
    }
}

TEST(ClearUpgrade, AnItemOutsideTheGearTemplateOfTheRoleIsNoUpgrade)
{
    GearScores scores = Scores("item head 10 10-20 0 30\n");
    EXPECT_FALSE(scores.IsClearUpgrade(11, {EmptyPosition()}));
}

TEST(ClearUpgrade, AnItemOfTheGearTemplateForAnEmptyPositionIsAClearUpgrade)
{
    GearScores scores = Scores("item head 10 10-20 0 30\n");
    EXPECT_TRUE(scores.IsClearUpgrade(10, {EmptyPosition()}));
}

TEST(ClearUpgrade, AnItemMustScoreAtLeastATenthMoreThanTheWornItem)
{
    GearScores scores = Scores(
        "item head 10 10-20 0 20\n"
        "item head 11 10-20 0 21.5\n"   // Less than a tenth more.
        "item head 12 10-20 0 22\n"     // A tenth more.
        "item head 13 10-20 0 15\n");   // Worse.
    GearOption const wornHelm = {{10, ItemGrade::High}};
    EXPECT_FALSE(scores.IsClearUpgrade(11, {wornHelm}));
    EXPECT_TRUE(scores.IsClearUpgrade(12, {wornHelm}));
    EXPECT_FALSE(scores.IsClearUpgrade(13, {wornHelm}));
    EXPECT_FALSE(scores.IsClearUpgrade(10, {wornHelm}));
}

TEST(ClearUpgrade, AWornGreyOrWhiteItemOutsideTheTemplateCountsAsNothing)
{
    GearScores scores = Scores("item chest 10 10-20 0 5\n");
    EXPECT_TRUE(scores.IsClearUpgrade(10, {{{99, ItemGrade::Low}}}));
}

TEST(ClearUpgrade, AWornGreenOrBetterItemOutsideTheTemplateIsNeverClearlyBeaten)
{
    GearScores scores = Scores("item chest 10 10-20 0 500\n");
    EXPECT_FALSE(scores.IsClearUpgrade(10, {{{99, ItemGrade::High}}}));
}

TEST(ClearUpgrade, ATwoHandedWeaponMustBeatBothItemsThatItReplaces)
{
    GearScores scores = Scores(
        "item mainhand 10 10-20 0 20\n"
        "item holdable 11 10-20 0 10\n"
        "item twohand 12 10-20 0 25\n"
        "item twohand 13 10-20 0 40\n");
    GearOption const maceAndOffHand = {{10, ItemGrade::High}, {11, ItemGrade::High}};
    EXPECT_FALSE(scores.IsClearUpgrade(12, {maceAndOffHand}));
    EXPECT_TRUE(scores.IsClearUpgrade(13, {maceAndOffHand}));
}

TEST(ClearUpgrade, AnItemWithTwoPositionsIsAClearUpgradeWhenItBeatsEitherWornItem)
{
    GearScores scores = Scores(
        "item finger 10 10-20 0 30\n"
        "item finger 11 10-20 0 10\n"
        "item finger 12 10-20 0 20\n");
    std::vector<GearOption> const rings = {{{10, ItemGrade::High}}, {{11, ItemGrade::High}}};
    EXPECT_TRUE(scores.IsClearUpgrade(12, rings));
}

namespace
{
    using Bots::BagItem;
    using Bots::BagRoom;
    using Bots::ChooseBagRoom;
    using Bots::IncomingItem;

    BagItem Grey(std::uint32_t value) { return {true, value}; }
    BagItem NotGrey(std::uint32_t value) { return {false, value}; }
}

TEST(BagFull, ABotDeletesItsCheapestGreyItemForAnItemThatIsWorthMore)
{
    std::vector<BagItem> const bag = {NotGrey(1), Grey(50), Grey(12), Grey(30)};
    BagRoom const room = ChooseBagRoom(bag, IncomingItem{13, false});
    EXPECT_TRUE(room.deletes);
    EXPECT_EQ(room.index, 2u);
}

TEST(BagFull, ABotKeepsItsGreyItemsForAnItemThatIsWorthLessAndThatItCannotUse)
{
    std::vector<BagItem> const bag = {Grey(20), Grey(12)};
    EXPECT_FALSE(ChooseBagRoom(bag, IncomingItem{12, false}).deletes);
}

TEST(BagFull, ABotDeletesItsCheapestGreyItemForAnItemThatItCanUse)
{
    std::vector<BagItem> const bag = {Grey(20), Grey(12)};
    BagRoom const room = ChooseBagRoom(bag, IncomingItem{0, true});
    EXPECT_TRUE(room.deletes);
    EXPECT_EQ(room.index, 1u);
}

TEST(BagFull, ABotWithNoGreyItemDeletesNothing)
{
    std::vector<BagItem> const bag = {NotGrey(1), NotGrey(2)};
    EXPECT_FALSE(ChooseBagRoom(bag, IncomingItem{1000, true}).deletes);
}

TEST(BagFull, OnEqualValuesTheFirstGreyItemGoes)
{
    std::vector<BagItem> const bag = {NotGrey(1), Grey(5), Grey(5)};
    EXPECT_EQ(ChooseBagRoom(bag, IncomingItem{6, false}).index, 1u);
}
