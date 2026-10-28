#include "DamageRules.h"
#include "FakeBot.h"
#include "GroupCoordinator.h"
#include "PlayerTank.h"

#include <gtest/gtest.h>

using Bots::BotContext;
using Bots::GroupCoordinator;
using Bots::NoGuid;
using Bots::Tests::FakeBot;

// jvbastel/WOWAI#189: the player tank. A human leads a group of bots with no
// bot tank and types no bot command: the focus is the tank's target, once it
// is an enemy in combat with the group, and the damage bots attack it after
// the tank has been top of its threat list for PlayerTankHoldMs.
namespace
{
    Bots::Guid const Human = 0x1;
    Bots::Guid const Rogue = 0x13;
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Kobold = 0xF130000302000022;
    Bots::Guid const Wolf = 0xF130000302000033;

    struct PlayerTankGroup
    {
        FakeBot fake{Rogue};
        GroupCoordinator group;

        PlayerTankGroup()
        {
            fake.SetGroupMembers({Human, Healer});
            fake.SetHealth(Human, 600, 600);
            fake.SetHealth(Healer, 300, 300);
            fake.SetHealth(Rogue, 400, 400);
            fake.SetHealth(Kobold, 200, 200);
            fake.SetHealth(Wolf, 200, 200);
            group.SetTank(Human, true);
        }

        // Enemy `enemy` fights the group: it attacks the tank, which has
        // threat on it.
        void Fights(Bots::Guid enemy, float tankRatio = Bots::UnrivalledThreat)
        {
            fake.SetVictim(enemy, Human);
            fake.SetThreatRatio(Human, enemy, tankRatio);
        }

        void Watch(std::int64_t nowMs) { Bots::WatchPlayerTank(fake, group, nowMs); }

        Bots::Guid Target(std::int64_t nowMs)
        {
            BotContext context{fake, fake, group, Bots::CommandState::Follow, Human, NoGuid};
            context.nowMs = nowMs;
            return Bots::PickDamageTarget(context);
        }
    };
}

TEST(PlayerTankFocus, TheFocusFollowsTheTanksTargetInCombat)
{
    PlayerTankGroup bots;
    bots.Fights(Kobold);
    bots.Fights(Wolf);

    bots.fake.SetSelectedTarget(Human, Kobold);
    bots.Watch(0);
    EXPECT_EQ(bots.group.GetFocusTarget(), Kobold);

    bots.fake.SetSelectedTarget(Human, Wolf);
    bots.Watch(100);
    EXPECT_EQ(bots.group.GetFocusTarget(), Wolf);
}

TEST(PlayerTankFocus, AFriendlyOrEmptyTargetKeepsTheLastFocus)
{
    PlayerTankGroup bots;
    bots.Fights(Kobold);
    bots.fake.SetSelectedTarget(Human, Kobold);
    bots.Watch(0);

    bots.fake.SetSelectedTarget(Human, Healer);
    bots.Watch(100);
    EXPECT_EQ(bots.group.GetFocusTarget(), Kobold);

    bots.fake.SetSelectedTarget(Human, NoGuid);
    bots.Watch(200);
    EXPECT_EQ(bots.group.GetFocusTarget(), Kobold);
}

TEST(PlayerTankFocus, AnEnemyNotInCombatWithTheGroupIsNotTheFocus)
{
    PlayerTankGroup bots;
    // The wolf stands in its pack: no fight, no threat of any member.
    bots.fake.SetSelectedTarget(Human, Wolf);
    bots.Watch(0);
    EXPECT_EQ(bots.group.GetFocusTarget(), NoGuid);

    // The wolf fights another player, out of the group.
    bots.fake.SetVictim(Wolf, 0x99);
    bots.Watch(100);
    EXPECT_EQ(bots.group.GetFocusTarget(), NoGuid);
}

TEST(PlayerTankFocus, AnEnemyThatHitsAMemberIsInCombatWithTheGroup)
{
    PlayerTankGroup bots;
    // The kobold hits the healer; the tank has no threat on it yet.
    bots.fake.SetVictim(Kobold, Healer);
    bots.fake.SetSelectedTarget(Human, Kobold);
    bots.Watch(0);
    EXPECT_EQ(bots.group.GetFocusTarget(), Kobold);
}

TEST(PlayerTankFocus, ABotTankGroupKeepsItsOwnFocus)
{
    PlayerTankGroup bots;
    bots.group.SetTank(0x11);
    bots.Fights(Kobold);
    bots.fake.SetSelectedTarget(Human, Kobold);
    bots.Watch(0);
    EXPECT_EQ(bots.group.GetFocusTarget(), NoGuid);
}

TEST(PlayerTankThreat, ADamageBotWaitsTwoSecondsOfTheTankOnTop)
{
    PlayerTankGroup bots;
    bots.Fights(Kobold, 1.4f);
    bots.fake.SetSelectedTarget(Human, Kobold);

    bots.Watch(0);
    EXPECT_EQ(bots.Target(0), NoGuid);
    bots.Watch(1900);
    EXPECT_EQ(bots.Target(1900), NoGuid);
    bots.Watch(2000);
    EXPECT_EQ(bots.Target(2000), Kobold);
}

TEST(PlayerTankThreat, TheWaitStartsWhenTheTankGetsOnTop)
{
    PlayerTankGroup bots;
    // The healer is above the tank on the kobold at first.
    bots.Fights(Kobold, 0.8f);
    bots.fake.SetSelectedTarget(Human, Kobold);
    bots.Watch(0);
    bots.Watch(1000);
    bots.fake.SetThreatRatio(Human, Kobold, 1.2f);
    bots.Watch(1500);
    bots.Watch(3000);
    EXPECT_EQ(bots.Target(3000), NoGuid);
    bots.Watch(3500);
    EXPECT_EQ(bots.Target(3500), Kobold);

    // The healer gets on top: the bots wait again.
    bots.fake.SetThreatRatio(Human, Kobold, 0.9f);
    bots.Watch(3600);
    EXPECT_EQ(bots.Target(3600), NoGuid);
}

TEST(PlayerTankThreat, ANewFocusStartsTheWaitAgain)
{
    PlayerTankGroup bots;
    bots.Fights(Kobold);
    bots.Fights(Wolf);
    bots.fake.SetSelectedTarget(Human, Kobold);
    bots.Watch(0);
    bots.Watch(2000);
    EXPECT_EQ(bots.Target(2000), Kobold);

    bots.fake.SetSelectedTarget(Human, Wolf);
    bots.Watch(2100);
    EXPECT_EQ(bots.Target(2100), NoGuid);
    bots.Watch(4100);
    EXPECT_EQ(bots.Target(4100), Wolf);
}
