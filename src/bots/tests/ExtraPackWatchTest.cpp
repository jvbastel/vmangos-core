#include "ExtraPackWatch.h"

#include <gtest/gtest.h>

using Bots::ExtraPackWatch;
using Bots::RunExtraPack;

namespace
{
    // Two packs of the route: pack14 with two mobs, pack15 with one.
    std::uint32_t const Pack14A = 48652;
    std::uint32_t const Pack14B = 48649;
    std::uint32_t const Pack15 = 48655;
    std::uint32_t const Roamer = 99999; // In no pack of the route.

    ExtraPackWatch Watch()
    {
        return ExtraPackWatch({{Pack14A, "pack14"}, {Pack14B, "pack14"}, {Pack15, "pack15"}});
    }
}

TEST(ExtraPackWatch, TheMobsOfThePulledPackAreNoExtraPack)
{
    ExtraPackWatch watch = Watch();
    watch.Start("pack14");
    watch.Observe(1000, {Pack14A, Pack14B});

    EXPECT_TRUE(watch.ExtraPacks().empty());
}

TEST(ExtraPackWatch, AnotherPackThatJoinsIsOneExtraPackFromItsFirstMob)
{
    ExtraPackWatch watch = Watch();
    watch.Start("pack15");
    watch.Observe(1000, {Pack15});
    watch.Observe(2500, {Pack15, Pack14A});
    watch.Observe(3000, {Pack15, Pack14A, Pack14B});

    ASSERT_EQ(watch.ExtraPacks().size(), 1u);
    EXPECT_EQ(watch.ExtraPacks()[0].pack, "pack14");
    EXPECT_EQ(watch.ExtraPacks()[0].ms, 2500);
    EXPECT_FALSE(watch.ExtraPacks()[0].warned);
}

TEST(ExtraPackWatch, AMobOfNoPackOfTheRouteIsNoExtraPack)
{
    ExtraPackWatch watch = Watch();
    watch.Start("pack15");
    watch.Observe(1000, {Pack15, Roamer});

    EXPECT_TRUE(watch.ExtraPacks().empty());
}

TEST(ExtraPackWatch, AWarnOfThePullMarksEachExtraPackOfIt)
{
    ExtraPackWatch watch = Watch();
    watch.Start("pack15");
    watch.NoteWarn(); // #105: the tank warns at the pull, before the pack joins.
    watch.Observe(2500, {Pack15, Pack14A});

    ASSERT_EQ(watch.ExtraPacks().size(), 1u);
    EXPECT_TRUE(watch.ExtraPacks()[0].warned);
}

TEST(ExtraPackWatch, ANewPullStartsWithNoExtraPackAndNoWarn)
{
    ExtraPackWatch watch = Watch();
    watch.Start("pack15");
    watch.NoteWarn();
    watch.Observe(2500, {Pack15, Pack14A});

    watch.Start("pack14");
    EXPECT_TRUE(watch.ExtraPacks().empty());
    watch.Observe(9000, {Pack14A, Pack15});

    ASSERT_EQ(watch.ExtraPacks().size(), 1u);
    EXPECT_EQ(watch.ExtraPacks()[0].pack, "pack15");
    EXPECT_FALSE(watch.ExtraPacks()[0].warned);
}

TEST(ExtraPackWatch, APullOfNoPackCountsEachPackThatJoins)
{
    ExtraPackWatch watch = Watch();
    watch.Start("");
    watch.Observe(1000, {Roamer, Pack15});

    ASSERT_EQ(watch.ExtraPacks().size(), 1u);
    EXPECT_EQ(watch.ExtraPacks()[0].pack, "pack15");
}

TEST(ExtraPackWatch, ThePackOfASpawn)
{
    ExtraPackWatch const watch = Watch();

    EXPECT_EQ(watch.PackOf(Pack14B), "pack14");
    EXPECT_EQ(watch.PackOf(Roamer), "");
}

// The warn hook of jvbastel/WOWAI#105: the line that the tank says in party
// chat when a pull brings extra packs.
TEST(ExtraPackWatch, TheWarnLineNamesTheNumberOfExtraPacks)
{
    EXPECT_EQ(Bots::ExtraPackWarnLine(1), "Careful: this pull brings 1 more pack.");
    EXPECT_EQ(Bots::ExtraPackWarnLine(2), "Careful: this pull brings 2 more packs.");
}

// jvbastel/WOWAI#123: the judge marks a pack when it joins, and a warn
// later in the pull keeps the mark.
TEST(ExtraPackWatch, KeepsTheMarkOfTheJudgeOfAnExtraPack)
{
    ExtraPackWatch watch = Watch();
    watch.Start("pack14");
    watch.Observe(1000, {Pack15});

    watch.Mark("pack15", {false, "a patrol brought it"});
    watch.NoteWarn();
    watch.Observe(2000, {Pack15});

    ASSERT_EQ(watch.ExtraPacks().size(), 1u);
    EXPECT_FALSE(watch.ExtraPacks()[0].avoidable);
    EXPECT_EQ(watch.ExtraPacks()[0].reason, "a patrol brought it");
    EXPECT_TRUE(watch.ExtraPacks()[0].warned);
}
