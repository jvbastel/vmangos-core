#include "DamageRules.h"
#include "FakeBot.h"
#include "GroupRules.h"

#include <gtest/gtest.h>

using Bots::BotContext;
using Bots::GroupCoordinator;
using Bots::NoGuid;
using Bots::PickDamageTarget;
using Bots::Tests::FakeBot;

// The targeting rules of the damage bots: they attack the target of the tank
// once the tank holds threat, or the target of the player after `attack`.
namespace
{
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Rogue = 0x13;
    Bots::Guid const Kobold = 0xF130000302000022;
    Bots::Guid const Wolf = 0xF130000302000033;

    struct DamageBot
    {
        FakeBot fake{Rogue};
        GroupCoordinator group;

        DamageBot()
        {
            fake.SetHealth(Rogue, 400, 400);
            fake.SetHealth(Kobold, 200, 200);
            fake.SetHealth(Wolf, 200, 200);
        }

        Bots::Guid Target(Bots::Guid attackTarget = NoGuid)
        {
            BotContext const context{fake, fake, group, Bots::CommandState::Follow, 0x1, attackTarget};
            return PickDamageTarget(context);
        }
    };
}

TEST(DamageTarget, NoTargetWithoutAFight)
{
    DamageBot bot;

    EXPECT_EQ(bot.Target(), NoGuid);
}

TEST(DamageTarget, WaitsWhileTheTankPulls)
{
    DamageBot bot;
    bot.group.StartPull(Tank, Kobold);

    EXPECT_EQ(bot.Target(), NoGuid);
}

TEST(DamageTarget, TheFocusTargetOnceTheTankHoldsThreat)
{
    DamageBot bot;
    bot.group.StartPull(Tank, Kobold);
    bot.group.HoldPull(Tank);

    EXPECT_EQ(bot.Target(), Kobold);
}

TEST(DamageTarget, TheFocusTargetWhenTheTankFightsBackOutsideAPull)
{
    DamageBot bot;
    bot.group.SetFocusTarget(Wolf);

    EXPECT_EQ(bot.Target(), Wolf);
}

// Outside a pull, the focus that still hits another member is not the
// tank's yet: the damage of the others would add to their threat on it, and
// the tank's taunt only evens the threat (WOWAI#73 review).
TEST(DamageTarget, WaitsWhileTheFocusOutsideAPullHitsAnotherMember)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.group.SetTank(Tank);
    bot.group.SetFocusTarget(Wolf);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), NoGuid);

    bot.fake.SetVictim(Wolf, Tank);

    EXPECT_EQ(bot.Target(), Wolf);
}

TEST(DamageTarget, TheFocusOnAnotherMemberWhenTheTankIsDead)
{
    DamageBot bot;
    bot.fake.SetHealth(Tank, 0, 600);
    bot.group.SetTank(Tank);
    bot.group.SetFocusTarget(Wolf);
    bot.fake.SetVictim(Wolf, 0x12);

    EXPECT_EQ(bot.Target(), Wolf);
}

TEST(DamageTarget, TheAttackTargetOfThePlayerComesFirst)
{
    DamageBot bot;
    bot.group.StartPull(Tank, Kobold);
    bot.group.HoldPull(Tank);

    EXPECT_EQ(bot.Target(Wolf), Wolf);
}

TEST(DamageTarget, TheAttackTargetDoesNotWaitForThreat)
{
    DamageBot bot;
    bot.group.StartPull(Tank, Kobold);

    EXPECT_EQ(bot.Target(Wolf), Wolf);
}

TEST(DamageTarget, BackToTheFocusTargetWhenTheAttackTargetDies)
{
    DamageBot bot;
    bot.group.StartPull(Tank, Kobold);
    bot.group.HoldPull(Tank);
    bot.fake.SetHealth(Wolf, 0, 200);

    EXPECT_EQ(bot.Target(Wolf), Kobold);
}

TEST(DamageTarget, FightsBackAnEnemyThatAttacksTheBot)
{
    DamageBot bot;
    bot.group.StartPull(Tank, Kobold);
    bot.fake.SetVictim(Wolf, Rogue);

    EXPECT_EQ(bot.Target(), Wolf);
}

// A damage bot that stays or follows does not see the tank's fight start: it
// joins when an enemy hits a group member (WOWAI#93).
TEST(DamageTarget, AttacksAnEnemyThatHitsAGroupMember)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), Wolf);
}

// The tank died in its fight: its focus is gone, and its pack beats the
// healer (WOWAI#93, Ragefire Chasm pack 9).
TEST(DamageTarget, AttacksAnEnemyOnAGroupMemberWhenTheTankIsDead)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 0, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.fake.SetDistance(Wolf, 60.0f);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), Wolf);
}

TEST(DamageTarget, AttacksAnEnemyOnAGroupMemberWhileTheTankPulls)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.StartPull(Tank, Kobold);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), Wolf);
}

// A fleeing enemy keeps its victim: far from the tank and from the member,
// it is out of the chase limit.
TEST(DamageTarget, LeavesAnEnemyOnAGroupMemberOutsideTheChaseLimit)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.fake.SetDistanceBetween(Wolf, Tank, Bots::ChaseLimitYards + 5.0f);
    bot.fake.SetDistanceBetween(Wolf, Healer, Bots::ChaseLimitYards + 5.0f);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), NoGuid);
}

// The bot waits for the taunt on the focus, but not on another enemy that
// hits a member.
TEST(DamageTarget, AttacksAnotherEnemyOnAMemberWhileItWaitsForTheTaunt)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.group.SetFocusTarget(Wolf);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), NoGuid);

    bot.fake.SetVictim(Kobold, Healer);

    EXPECT_EQ(bot.Target(), Kobold);
}

// A loose enemy on a member comes before the focus that the tank holds: no
// bot attacked the second Earthborer on the healer while all hit the focus,
// a lost mob (WOWAI#93, Ragefire Chasm pack 1).
TEST(DamageTarget, ALooseEnemyOnAMemberComesBeforeTheFocus)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.group.StartPull(Tank, Kobold);
    bot.group.HoldPull(Tank);
    bot.fake.SetVictim(Kobold, Tank);
    bot.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(bot.Target(), Wolf);
}

// Two loose enemies: the bot takes the one that no other member attacks, so
// that no enemy stays lost.
TEST(DamageTarget, TakesTheLooseEnemyThatNoOtherMemberAttacks)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Mage = 0x14;
    Bots::Guid const Trogg = 0xF130000302000044;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetHealth(Mage, 300, 300);
    bot.fake.SetHealth(Trogg, 200, 200);
    bot.fake.SetGroupMembers({Tank, Healer, Mage});
    bot.group.SetTank(Tank);
    bot.fake.SetVictim(Wolf, Healer);
    bot.fake.SetVictim(Trogg, Healer);
    bot.fake.SetVictim(Mage, Wolf);

    EXPECT_EQ(bot.Target(), Trogg);
}

// The bot keeps the loose enemy that only it attacks.
TEST(DamageTarget, KeepsTheLooseEnemyThatOnlyItAttacks)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Trogg = 0xF130000302000044;
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetHealth(Trogg, 200, 200);
    bot.fake.SetGroupMembers({Tank, Healer});
    bot.group.SetTank(Tank);
    bot.fake.SetVictim(Wolf, Healer);
    bot.fake.SetVictim(Trogg, Healer);
    bot.fake.SetVictim(Rogue, Trogg);

    EXPECT_EQ(bot.Target(), Trogg);
}

// The attacks of the player hold no enemy in a scripted run (the test driver
// is no bot): the bot does not leave an enemy to it.
TEST(DamageTarget, ALooseEnemyThatThePlayerAttacksIsStillLoose)
{
    DamageBot bot;
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Player = 0x1;
    Bots::Guid const Mage = 0x14;
    Bots::Guid const Trogg = 0xF130000302000044;
    bot.fake.SetGroupMembers({Tank, Healer, Player, Mage});
    bot.fake.SetHealth(Mage, 300, 300);
    bot.fake.SetHealth(Tank, 600, 600);
    bot.fake.SetHealth(Healer, 300, 300);
    bot.fake.SetHealth(Player, 300, 300);
    bot.fake.SetHealth(Trogg, 200, 200);
    bot.group.SetTank(Tank);
    bot.fake.SetVictim(Wolf, Healer);
    bot.fake.SetVictim(Trogg, Healer);
    bot.fake.SetVictim(Player, Trogg);
    bot.fake.SetVictim(Mage, Wolf);

    EXPECT_EQ(bot.Target(), Trogg);
}

// Two bots on one loose enemy, and another loose enemy that nobody attacks:
// only one of them moves on, so that they do not swap back and forth. The
// bot with the lower guid keeps the enemy.
TEST(DamageTarget, OfTwoBotsOnOneLooseEnemyTheLowerGuidKeepsIt)
{
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Mage = 0x14;
    Bots::Guid const Trogg = 0xF130000302000044;
    // The same fight, seen by the rogue (0x13) and by the mage (0x14).
    DamageBot rogue;
    DamageBot mage;
    mage.fake = FakeBot{Mage};
    for (DamageBot* bot : {&rogue, &mage})
    {
        bot->fake.SetGroupMembers({Tank, Healer, bot == &rogue ? Mage : Rogue});
        for (Bots::Guid unit : {Tank, Healer, Mage, Rogue, Wolf, Trogg})
            bot->fake.SetHealth(unit, 300, 300);
        bot->group.SetTank(Tank);
        bot->fake.SetVictim(Wolf, Healer);
        bot->fake.SetVictim(Trogg, Healer);
        bot->fake.SetVictim(Rogue, Trogg);
        bot->fake.SetVictim(Mage, Trogg);
    }

    EXPECT_EQ(rogue.Target(), Trogg);
    EXPECT_EQ(mage.Target(), Wolf);
}
