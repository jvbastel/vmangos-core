#include "ThreatWatch.h"

#include <gtest/gtest.h>

using Bots::EnemyOnMember;
using Bots::ThreatWatch;

// A threat loss: an enemy hits a group member who is not the tank for more
// than 4 seconds (spec section 10, pass thresholds).
namespace
{
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Rogue = 0x13;
    Bots::Guid const Enforcer = 0xF130000302000022;
    Bots::Guid const Other = 0xF130000302000033;
}

TEST(ThreatWatch, NoLossWithoutHits)
{
    ThreatWatch watch;
    watch.Update({}, 0);
    watch.Update({}, 10000);

    EXPECT_TRUE(watch.Losses().empty());
}

TEST(ThreatWatch, AShortHitIsNoLoss)
{
    ThreatWatch watch;
    watch.Update({{Enforcer, Healer}}, 0);
    watch.Update({{Enforcer, Healer}}, 4000);
    watch.Update({}, 4500);

    EXPECT_TRUE(watch.Losses().empty());
}

TEST(ThreatWatch, AHitOfMoreThanFourSecondsIsALoss)
{
    ThreatWatch watch;
    watch.Update({{Enforcer, Healer}}, 1000);
    watch.Update({{Enforcer, Healer}}, 5001);

    ASSERT_EQ(watch.Losses().size(), 1u);
    EXPECT_EQ(watch.Losses()[0].enemy, Enforcer);
    EXPECT_EQ(watch.Losses()[0].member, Healer);
    EXPECT_EQ(watch.Losses()[0].startMs, 1000);
}

TEST(ThreatWatch, ALongHitCountsOnceAndKeepsItsEnd)
{
    ThreatWatch watch;
    for (std::int64_t t = 0; t <= 120000; t += 500)
        watch.Update({{Enforcer, Healer}}, t);
    watch.Update({}, 120500);

    ASSERT_EQ(watch.Losses().size(), 1u);
    EXPECT_EQ(watch.Losses()[0].endMs, 120000);
}

TEST(ThreatWatch, AHitAgainAfterAGapStartsANewCount)
{
    ThreatWatch watch;
    watch.Update({{Enforcer, Healer}}, 0);
    watch.Update({{Enforcer, Healer}}, 3000);
    watch.Update({}, 3500);
    watch.Update({{Enforcer, Healer}}, 4000);
    watch.Update({{Enforcer, Healer}}, 7000);

    EXPECT_TRUE(watch.Losses().empty());
}

TEST(ThreatWatch, EachEnemyAndMemberCountsApart)
{
    ThreatWatch watch;
    watch.Update({{Enforcer, Healer}, {Other, Rogue}}, 0);
    watch.Update({{Enforcer, Rogue}, {Other, Rogue}}, 3000);
    watch.Update({{Enforcer, Rogue}, {Other, Rogue}}, 6000);

    ASSERT_EQ(watch.Losses().size(), 1u);
    EXPECT_EQ(watch.Losses()[0].enemy, Other);
}
