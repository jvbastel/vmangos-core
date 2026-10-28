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
    EXPECT_FALSE(group.TankHoldsThreat(0));
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
    EXPECT_FALSE(group.TankHoldsThreat(0));
}

// jvbastel/WOWAI#127: the target that attacks the puller holds the pull,
// but the tank holds threat only HoldContactMs after the melee contact.
TEST(GroupCoordinator, ThePullerHoldsThreatAfterTheMeleeContact)
{
    GroupCoordinator group;
    group.StartPull(Tank, Kobold);

    group.HoldPull(Tank, 1000);
    EXPECT_EQ(group.GetPull().phase, PullPhase::Holding);
    EXPECT_FALSE(group.TankHoldsThreat(1000));

    group.NoteFocusContact(3000);
    group.NoteFocusContact(4000);
    EXPECT_EQ(group.GetPull().contactMs, 3000);
    EXPECT_FALSE(group.TankHoldsThreat(3000 + Bots::HoldContactMs - 1));
    EXPECT_TRUE(group.TankHoldsThreat(3000 + Bots::HoldContactMs));
}

TEST(GroupCoordinator, NoMeleeContactBeforeTheHold)
{
    GroupCoordinator group;
    group.StartPull(Tank, Kobold);

    group.NoteFocusContact(500);

    EXPECT_FALSE(group.GetPull().hasContact);
}

TEST(GroupCoordinator, ThePullerHoldsThreatAfterTheTimeLimitWithNoContact)
{
    GroupCoordinator group;
    group.StartPull(Tank, Kobold);
    group.HoldPull(Tank, 1000);

    EXPECT_FALSE(group.TankHoldsThreat(1000 + Bots::HoldLimitMs - 1));
    EXPECT_TRUE(group.TankHoldsThreat(1000 + Bots::HoldLimitMs));
}

// Review of jvbastel/WOWAI#127 and #121 (T2): the two clocks from the
// engage. The tank goes in to a caster that stays away CasterComeMs after
// it, once back at its fight spot; the damage bots open on a target that
// never comes HoldLimitMs after it, also once the tank is back. So they
// never open on a caster before the tank went in.
TEST(GroupCoordinator, TheTimeLimitWaitsForTheTankBackAtItsFightSpot)
{
    static_assert(Bots::CasterComeMs < Bots::HoldLimitMs, "the tank goes in before the damage bots open");
    GroupCoordinator group;
    group.StartPull(Tank, Kobold);
    group.SetFightSpot(Tank, {10, 0, 0});
    group.HoldPull(Tank, 1000);

    EXPECT_FALSE(group.TankHoldsThreat(1000 + Bots::HoldLimitMs + 3000));

    group.ClearFightSpot(Tank);
    EXPECT_EQ(group.TankHoldsThreatBy(1000 + Bots::HoldLimitMs + 3000), Bots::ThreatHeld::TimeLimit);
}

TEST(GroupCoordinator, ANewPullForgetsTheMeleeContact)
{
    GroupCoordinator group;
    group.StartPull(Tank, Kobold);
    group.HoldPull(Tank, 1000);
    group.NoteFocusContact(2000);

    group.StartPull(Tank, Other);

    EXPECT_FALSE(group.GetPull().hasContact);
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

    EXPECT_TRUE(group.TankHoldsThreat(0));
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

    bool FirstFree(PullState const&, std::vector<Bots::Position> const& taken, Bots::Position& spot)
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

    Bots::MemberSpot const again = group.ClaimMemberSpot(Ranged, [&finds](PullState const& pull, std::vector<Bots::Position> const& taken, Bots::Position& spot) {
        ++finds;
        return FirstFree(pull, taken, spot);
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

// The search runs outside the lock: it may ask the coordinator, and it gets
// the pull that is on.
TEST(GroupCoordinator, AMemberSpotSearchMayAskTheCoordinator)
{
    GroupCoordinator group;
    group.StartPull(Tank, Kobold);

    Bots::MemberSpot const spot = group.ClaimMemberSpot(Ranged,
        [&group](PullState const& pull, std::vector<Bots::Position> const& taken, Bots::Position& at) {
            return pull.target == Kobold && group.GetPull().target == Kobold && FirstFree(pull, taken, at);
        });

    EXPECT_TRUE(spot.found);
}

// jvbastel/WOWAI#127: the tank is back at its fight spot once the rule
// "back at the fight spot" clears it; a pull held with no fight spot has no
// walk back.
TEST(GroupCoordinator, TheTankIsBackAtItsFightSpotOnceItClearsIt)
{
    GroupCoordinator group;
    group.StartPull(Tank, Kobold);
    group.SetFightSpot(Tank, {1.0f, 2.0f, 3.0f});
    group.HoldPull(Tank, 1000);
    EXPECT_FALSE(group.GetPull().backAtFightSpot);

    group.ClearFightSpot(Tank);

    EXPECT_TRUE(group.GetPull().backAtFightSpot);
}

TEST(GroupCoordinator, APullHeldWithNoFightSpotIsBackAtOnce)
{
    GroupCoordinator group;
    group.StartPull(Tank, Kobold);
    group.HoldPull(Tank, 1000);

    EXPECT_TRUE(group.GetPull().backAtFightSpot);
}

TEST(GroupCoordinator, ANewPullIsNotBackAtItsFightSpot)
{
    GroupCoordinator group;
    group.StartPull(Tank, Kobold);
    group.HoldPull(Tank, 1000);
    group.EndPull();
    group.StartPull(Tank, Other);

    EXPECT_FALSE(group.GetPull().backAtFightSpot);
}

// jvbastel/WOWAI#143: each bot sets its role; a member with no role (a
// human player) has none.
TEST(GroupCoordinator, KeepsTheRoleThatEachMemberSets)
{
    GroupCoordinator group;
    group.SetMemberRole(Tank, Bots::BotRole::Tank);

    EXPECT_TRUE(group.HasRole(Tank, Bots::BotRole::Tank));
    EXPECT_FALSE(group.HasRole(Tank, Bots::BotRole::Ranged));
    EXPECT_FALSE(group.HasRole(0x21, Bots::BotRole::Ranged));
}

// jvbastel/WOWAI#189: a player-tank group is a group whose tank is a human
// (GroupTank.h, IsHumanTank). A bot tank, or no tank, makes none.
TEST(GroupCoordinatorPlayerTank, OnlyAHumanTankMakesAPlayerTankGroup)
{
    Bots::GroupCoordinator group;
    EXPECT_FALSE(group.IsPlayerTankGroup());
    group.SetTank(0x1, true);
    EXPECT_TRUE(group.IsPlayerTankGroup());
    group.SetTank(0x11);
    EXPECT_FALSE(group.IsPlayerTankGroup());
}
