#include "ClassModules.h"
#include "FakeBot.h"
#include "PriestModule.h"

#include <gtest/gtest.h>

using Bots::BotContext;
using Bots::CommandState;
using Bots::GroupCoordinator;
using Bots::StrategyStack;
using Bots::Tests::FakeBot;

// The walk of the group behind the tank on a pull (WOWAI#92): while the tank
// runs to a pack and fights it, the damage bots and the healer follow it at a
// distance, out of the aggro distance of the enemies.
namespace
{
    Bots::Guid const Self = 0x11;
    Bots::Guid const Player = 0x05;
    Bots::Guid const Tank = 0x21;
    Bots::Guid const Kobold = 0xF130000302000022;

    // The bot at the origin, the player beside it, and the tank 50 yards
    // north of it on its way to the kobold.
    struct Bot
    {
        FakeBot fake{Self};
        GroupCoordinator group;
        StrategyStack stack;

        Bot()
        {
            stack.Push(Bots::MakeCommandStrategy());
            fake.SetHealth(Self, 600, 600);
            fake.SetHealth(Player, 600, 600);
            fake.SetHealth(Tank, 600, 600);
            fake.SetHealth(Kobold, 300, 300);
            fake.SetPosition(Self, {0.0f, 0.0f, 0.0f});
            fake.SetPosition(Player, {2.0f, 0.0f, 0.0f});
            fake.SetPosition(Tank, {0.0f, 50.0f, 0.0f});
            fake.SetPosition(Kobold, {0.0f, 70.0f, 0.0f});
            fake.SetGroupMembers({Tank});
            group.SetTank(Tank);
            group.StartPull(Tank, Kobold);
        }

        std::string Think(CommandState command)
        {
            BotContext const context{fake, fake, group, command, Player};
            return stack.Think(context);
        }
    };
}

TEST(FollowTank, ABotThatStaysFollowsTheTankThatRunsToThePack)
{
    Bot bot;

    EXPECT_EQ(bot.Think(CommandState::Stay), "follow the tank to the pull");
    // On the way to the tank, 12 yards short of it.
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"keep move to 0 38 0"}));
}

TEST(FollowTank, AFollowingBotKeepsUpWithTheTankToo)
{
    Bot bot;

    EXPECT_EQ(bot.Think(CommandState::Follow), "follow the tank to the pull");
}

TEST(FollowTank, TheBotStopsOutOfTheAggroDistanceOfThePack)
{
    Bot bot;
    // The tank is 10 yards from the kobold, which pulls a bot from 20 yards.
    bot.fake.SetPosition(Tank, {0.0f, 60.0f, 0.0f});
    bot.fake.AddEnemy({0.0f, 70.0f, 0.0f}, 20.0f);

    EXPECT_EQ(bot.Think(CommandState::Stay), "follow the tank to the pull");
    // 25 yards from the kobold: its aggro distance and the margin.
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"keep move to 0 45 0"}));
}

TEST(FollowTank, InTheFightTheBotComesNearToThePackOnTheTank)
{
    Bot bot;
    bot.fake.SetPosition(Tank, {0.0f, 60.0f, 0.0f});
    bot.fake.SetPosition(Kobold, {0.0f, 62.0f, 0.0f});
    bot.fake.AddEnemy({0.0f, 62.0f, 0.0f}, 20.0f);
    bot.fake.SetVictim(Kobold, Tank);
    bot.group.HoldPull(Tank);

    EXPECT_EQ(bot.Think(CommandState::Stay), "follow the tank to the pull");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"keep move to 0 48 0"}));
}

TEST(FollowTank, AnotherPackNearTheTankStillCounts)
{
    Bot bot;
    bot.fake.SetPosition(Tank, {0.0f, 60.0f, 0.0f});
    bot.fake.SetPosition(Kobold, {0.0f, 62.0f, 0.0f});
    bot.fake.SetVictim(Kobold, Tank);
    bot.group.HoldPull(Tank);
    // A pack that is not in the fight, 12 yards west of the tank's path.
    bot.fake.AddEnemy({-12.0f, 45.0f, 0.0f}, 10.0f);

    bot.Think(CommandState::Stay);
    // The first spot whose walk stays 15 yards from it.
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"keep move to 0 36 0"}));
}

TEST(FollowTank, ABotNearTheTankDoesNotMove)
{
    Bot bot;
    // A short pull: the tank stays in casting range of the group.
    bot.fake.SetPosition(Tank, {0.0f, 28.0f, 0.0f});

    EXPECT_NE(bot.Think(CommandState::Stay), "follow the tank to the pull");
}

TEST(FollowTank, NobodyFollowsWithoutAPull)
{
    Bot bot;
    bot.group.EndPull();

    EXPECT_NE(bot.Think(CommandState::Stay), "follow the tank to the pull");
}

TEST(FollowTank, NobodyFollowsADeadTank)
{
    Bot bot;
    bot.fake.SetHealth(Tank, 0, 600);

    EXPECT_NE(bot.Think(CommandState::Stay), "follow the tank to the pull");
}

TEST(FollowTank, ABotSentByComeDoesNotFollow)
{
    Bot bot;

    EXPECT_NE(bot.Think(CommandState::Come), "follow the tank to the pull");
}

TEST(FollowTank, TheTankItselfDoesNotFollow)
{
    Bot bot;
    bot.group.StartPull(Self, Kobold);

    EXPECT_NE(bot.Think(CommandState::Stay), "follow the tank to the pull");
}

TEST(FollowTank, WithNoSafeSpotTheBotStays)
{
    Bot bot;
    // The kobold pulls a bot from 66 yards: no spot on the way is safe.
    bot.fake.AddEnemy({0.0f, 70.0f, 0.0f}, 66.0f);

    EXPECT_EQ(bot.Think(CommandState::Stay), "");
}

TEST(FollowTank, AHealerHealsTheHurtTankBeforeItFollows)
{
    Bots::Tests::FakeBot fake{Self};
    GroupCoordinator group;
    StrategyStack stack = Bots::MakeStrategyStack(Bots::BotClass::Priest, Bots::BotRole::Healer);
    fake.SetHealth(Self, 600, 600);
    fake.SetMana(Self, 600, 600);
    fake.SetHealth(Tank, 200, 600);
    fake.SetHealth(Kobold, 300, 300);
    fake.SetPosition(Self, {0.0f, 0.0f, 0.0f});
    fake.SetPosition(Tank, {0.0f, 30.0f, 0.0f});
    fake.SetPosition(Kobold, {0.0f, 32.0f, 0.0f});
    fake.SetGroupMembers({Tank});
    fake.SetKnownSpell(Bots::Priest::LesserHeal);
    group.SetTank(Tank);
    group.StartPull(Tank, Kobold);
    BotContext const context{fake, fake, group, CommandState::Stay, Player};

    EXPECT_NE(stack.Think(context), "follow the tank to the pull");
    EXPECT_NE(stack.Think(context), "");
}
