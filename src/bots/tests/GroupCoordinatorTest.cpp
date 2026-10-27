#include "GroupCoordinator.h"

#include <gtest/gtest.h>

#include <algorithm>
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

namespace
{
    Bots::Guid const Boss = 0xF130002CFD000101; // Oggleflint, 11517.
    Bots::Guid const Winner = 0x12;
}

TEST(GroupCoordinator, EachBotThatSeesABossKillAddsOneEvent)
{
    GroupCoordinator group;

    group.NoteBossKill(Boss, 11517, 1000);
    group.NoteBossKill(Boss, 11517, 1050);
    group.NoteBossKill(Boss, 11517, 1100);

    std::vector<Bots::GroupEvent> const events = group.TakeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, Bots::GroupEventKind::BossKill);
    EXPECT_EQ(events[0].target, Boss);
    EXPECT_EQ(events[0].entry, 11517u);
}

TEST(GroupCoordinator, ALateBotDoesNotRepeatABossKillThatWasTaken)
{
    GroupCoordinator group;
    group.NoteBossKill(Boss, 11517, 1000);
    group.TakeEvents();

    group.NoteBossKill(Boss, 11517, 1200);

    EXPECT_TRUE(group.TakeEvents().empty());
}

TEST(GroupCoordinator, AKillOfTheSameCreatureLongAfterIsANewEvent)
{
    GroupCoordinator group;
    group.NoteBossKill(Boss, 11517, 1000);
    group.TakeEvents();

    // The creature respawned and died again.
    group.NoteBossKill(Boss, 11517, 1000 + Bots::GroupEventMemoryMs + 1);

    EXPECT_EQ(group.TakeEvents().size(), 1u);
}

TEST(GroupCoordinator, EachBotThatSeesARollEndAddsOneEvent)
{
    GroupCoordinator group;

    group.NoteRollWon(Kobold, 0, 2140, Winner, 2000);
    group.NoteRollWon(Kobold, 0, 2140, Winner, 2000);
    group.NoteRollWon(Kobold, 1, 3196, Tank, 2000);

    std::vector<Bots::GroupEvent> const events = group.TakeEvents();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].kind, Bots::GroupEventKind::LootRoll);
    EXPECT_EQ(events[0].entry, 2140u);
    EXPECT_EQ(events[0].winner, Winner);
    EXPECT_EQ(events[1].entry, 3196u);
    EXPECT_EQ(events[1].winner, Tank);
}

// A bot that died, left the group, or logged out while it rested: the run
// tells the coordinator, so that `wait rested` does not wait for it for good
// (WOWAI#94 review).
TEST(GroupCoordinator, AForgottenMemberRestsNoMore)
{
    GroupCoordinator group;
    group.SetResting(Tank, Bots::RestReason::Eat);
    group.SetRestUntilFull(0x12);

    group.ForgetRest(Tank);
    group.ForgetRest(0x12);

    EXPECT_TRUE(group.GetResting().empty());
    EXPECT_FALSE(group.RestsUntilFull(0x12));
}

// Member spots (MemberSpot.h): the read of the taken spots and the note of
// the bot's own spot are one step (CODING_STANDARDS 6, the review of
// jvbastel/WOWAI#100). Each bot takes the first of two spots that is free.
namespace
{
    Bots::Guid const Ranged = 0x12;
    Bots::Guid const Healer = 0x13;

    bool FirstFree(std::vector<Bots::Position> const& taken, Bots::Position& spot)
    {
        for (float x : {-16.0f, -22.0f})
            if (std::none_of(taken.begin(), taken.end(), [x](Bots::Position const& other) { return other.x == x; }))
            {
                spot = {x, 0, 0};
                return true;
            }
        return false;
    }
}

TEST(GroupCoordinator, TwoBotsBackToBackClaimTwoMemberSpots)
{
    GroupCoordinator group;

    Bots::MemberSpot const ranged = group.ClaimMemberSpot(Ranged, FirstFree);
    Bots::MemberSpot const healer = group.ClaimMemberSpot(Healer, FirstFree);

    EXPECT_EQ(ranged.spot.x, -16.0f);
    EXPECT_EQ(healer.spot.x, -22.0f);
    EXPECT_TRUE(ranged.found && healer.found);
}

TEST(GroupCoordinator, ABotWithAMemberSpotKeepsIt)
{
    GroupCoordinator group;
    group.ClaimMemberSpot(Ranged, FirstFree);
    int finds = 0;

    Bots::MemberSpot const again = group.ClaimMemberSpot(Ranged, [&finds](std::vector<Bots::Position> const& taken, Bots::Position& spot) {
        ++finds;
        return FirstFree(taken, spot);
    });

    EXPECT_EQ(finds, 0);
    EXPECT_EQ(again.spot.x, -16.0f);
}

TEST(GroupCoordinator, TwoBotsAtOnceNeverClaimOneMemberSpot)
{
    for (int round = 0; round < 500; ++round)
    {
        GroupCoordinator group;
        Bots::MemberSpot healer;
        std::thread other([&] { healer = group.ClaimMemberSpot(Healer, FirstFree); });
        Bots::MemberSpot const ranged = group.ClaimMemberSpot(Ranged, FirstFree);
        other.join();
        ASSERT_NE(ranged.spot.x, healer.spot.x);
    }
}
