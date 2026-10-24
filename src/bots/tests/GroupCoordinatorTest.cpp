#include "GroupCoordinator.h"

#include <gtest/gtest.h>

#include <thread>

using Bots::GroupCoordinator;
using Bots::NoGuid;
using Bots::PullPhase;
using Bots::PullState;

namespace
{
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Kobold = 0xF130000302000022;
    Bots::Guid const Other = 0xF130000302000033;
}

TEST(GroupCoordinator, StartsWithNoFocusAndNoPull)
{
    GroupCoordinator group;

    EXPECT_EQ(group.GetFocusTarget(), NoGuid);
    EXPECT_EQ(group.GetPull().phase, PullPhase::None);
    EXPECT_FALSE(group.TankHoldsThreat());
}

TEST(GroupCoordinator, APullMakesItsTargetTheFocusTarget)
{
    GroupCoordinator group;

    group.StartPull(Tank, Kobold);

    PullState const pull = group.GetPull();
    EXPECT_EQ(pull.phase, PullPhase::Pulling);
    EXPECT_EQ(pull.puller, Tank);
    EXPECT_EQ(pull.target, Kobold);
    EXPECT_EQ(group.GetFocusTarget(), Kobold);
    EXPECT_FALSE(group.TankHoldsThreat());
}

TEST(GroupCoordinator, ThePullerHoldsThreatWhenTheTargetAttacksIt)
{
    GroupCoordinator group;
    group.StartPull(Tank, Kobold);

    group.HoldPull(Tank);

    EXPECT_EQ(group.GetPull().phase, PullPhase::Holding);
    EXPECT_TRUE(group.TankHoldsThreat());
}

TEST(GroupCoordinator, OnlyThePullerCanHoldThePull)
{
    GroupCoordinator group;
    group.StartPull(Tank, Kobold);

    group.HoldPull(0x22);

    EXPECT_EQ(group.GetPull().phase, PullPhase::Pulling);
}

TEST(GroupCoordinator, EndingThePullClearsThePullAndTheFocus)
{
    GroupCoordinator group;
    group.StartPull(Tank, Kobold);
    group.HoldPull(Tank);

    group.EndPull();

    EXPECT_EQ(group.GetPull().phase, PullPhase::None);
    EXPECT_EQ(group.GetFocusTarget(), NoGuid);
}

TEST(GroupCoordinator, ANewPullReplacesTheOldOne)
{
    GroupCoordinator group;
    group.StartPull(Tank, Kobold);
    group.HoldPull(Tank);

    group.StartPull(Tank, Other);

    EXPECT_EQ(group.GetPull().phase, PullPhase::Pulling);
    EXPECT_EQ(group.GetPull().target, Other);
    EXPECT_EQ(group.GetFocusTarget(), Other);
}

TEST(GroupCoordinator, BotsOnTwoMapThreadsSeeAWholePull)
{
    // The bots of one group can briefly sit on two map threads. A reader
    // never sees a pull whose target is the focus of another pull.
    GroupCoordinator group;
    std::thread writer([&group] {
        for (int i = 0; i < 20000; ++i)
            group.StartPull(Tank, i % 2 ? Kobold : Other);
    });
    for (int i = 0; i < 20000; ++i)
    {
        PullState const pull = group.GetPull();
        EXPECT_TRUE(pull.phase == PullPhase::None || pull.target == Kobold || pull.target == Other);
    }
    writer.join();
}

TEST(GroupCoordinator, TheTankHoldsThreatOnAFocusTargetOutsideAPull)
{
    // The tank fights back an enemy that attacks it.
    GroupCoordinator group;

    group.SetFocusTarget(Kobold);

    EXPECT_TRUE(group.TankHoldsThreat());
}

TEST(GroupCoordinator, StartsWithNoBossFight)
{
    GroupCoordinator group;

    EXPECT_EQ(group.GetBossFight().boss, NoGuid);
    EXPECT_EQ(group.GetBossFight().entry, 0u);
}

TEST(GroupCoordinator, KeepsTheFirstBossFightUntilItEnds)
{
    GroupCoordinator group;

    EXPECT_TRUE(group.StartBossFight(Kobold, 11520));
    EXPECT_FALSE(group.StartBossFight(Other, 11518));

    EXPECT_EQ(group.GetBossFight().boss, Kobold);
    EXPECT_EQ(group.GetBossFight().entry, 11520u);

    group.EndBossFight(Other);
    EXPECT_EQ(group.GetBossFight().boss, Kobold);
    group.EndBossFight(Kobold);
    EXPECT_EQ(group.GetBossFight().boss, NoGuid);
    EXPECT_TRUE(group.StartBossFight(Other, 11518));
}
