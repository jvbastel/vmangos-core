#include "ChaseWatch.h"

#include <gtest/gtest.h>

using Bots::ChaseWatch;

// Chases beyond the limit: no bot chases more than 25 yards from the tank
// (spec section 10, pass thresholds).
namespace
{
    Bots::Guid const Rogue = 0x13;
    Bots::Guid const Warlock = 0x14;
    Bots::Guid const Enforcer = 0xF130000302000022;
}

TEST(ChaseWatch, AChaseNearTheTankIsFine)
{
    ChaseWatch watch;
    watch.Update({{Rogue, Enforcer, 10.0f}}, 0);
    watch.Update({{Rogue, Enforcer, 25.0f}}, 1000);
    watch.Update({}, 2000);

    EXPECT_TRUE(watch.Chases().empty());
}

TEST(ChaseWatch, AChaseBeyondTheLimitCountsOnceWithItsFarthestPoint)
{
    ChaseWatch watch;
    watch.Update({{Rogue, Enforcer, 20.0f}}, 0);
    watch.Update({{Rogue, Enforcer, 26.0f}}, 1000);
    watch.Update({{Rogue, Enforcer, 31.5f}}, 2000);
    watch.Update({{Rogue, Enforcer, 28.0f}}, 3000);
    watch.Update({{Rogue, Enforcer, 12.0f}}, 4000);

    ASSERT_EQ(watch.Chases().size(), 1u);
    EXPECT_EQ(watch.Chases()[0].member, Rogue);
    EXPECT_EQ(watch.Chases()[0].enemy, Enforcer);
    EXPECT_EQ(watch.Chases()[0].startMs, 1000);
    EXPECT_EQ(watch.Chases()[0].endMs, 3000);
    EXPECT_FLOAT_EQ(watch.Chases()[0].maxYards, 31.5f);
}

TEST(ChaseWatch, AChaseThatStopsEndsAndANewOneCountsAgain)
{
    ChaseWatch watch;
    watch.Update({{Rogue, Enforcer, 30.0f}}, 0);
    watch.Update({}, 1000);
    watch.Update({{Rogue, Enforcer, 30.0f}}, 2000);

    ASSERT_EQ(watch.Chases().size(), 2u);
    EXPECT_EQ(watch.Chases()[1].startMs, 2000);
}

TEST(ChaseWatch, EachMemberHasItsOwnChase)
{
    ChaseWatch watch;
    watch.Update({{Rogue, Enforcer, 30.0f}, {Warlock, Enforcer, 27.0f}}, 0);
    watch.Update({{Rogue, Enforcer, 30.0f}, {Warlock, Enforcer, 27.0f}}, 1000);

    ASSERT_EQ(watch.Chases().size(), 2u);
    EXPECT_EQ(watch.Chases()[0].member, Rogue);
    EXPECT_EQ(watch.Chases()[1].member, Warlock);
    EXPECT_EQ(watch.Chases()[1].endMs, 1000);
}
