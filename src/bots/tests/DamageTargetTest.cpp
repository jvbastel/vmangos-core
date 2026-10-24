#include "CommonRules.h"
#include "FakeBot.h"

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
