#include "ClassModules.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

using Bots::BotContext;
using Bots::CommandState;
using Bots::GroupCoordinator;
using Bots::StrategyStack;
using Bots::Tests::FakeBot;

namespace
{
    Bots::Guid const Self = 0x11;
    Bots::Guid const Player = 0x05;
    Bots::Guid const Kobold = 0xF130000302000022;

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
            fake.SetDistance(Player, 30.0f);
        }

        std::string Think(CommandState command)
        {
            BotContext const context{fake, fake, group, command, Player};
            return stack.Think(context);
        }
    };
}

TEST(CommandStrategy, AFollowingBotFollowsThePlayerAgain)
{
    Bot bot;

    bot.Think(CommandState::Follow);

    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"follow 0x0000000000000005"}));
}

TEST(CommandStrategy, ABotThatFollowsAlreadyDoesNothing)
{
    Bot bot;
    bot.fake.SetFollowTarget(Player);

    EXPECT_EQ(bot.Think(CommandState::Follow), "");
}

TEST(CommandStrategy, ABotDoesNotFollowInAFight)
{
    Bot bot;
    bot.fake.SetInCombat(Self);

    EXPECT_EQ(bot.Think(CommandState::Follow), "");
}

TEST(CommandStrategy, ThePullerDoesNotFollowDuringItsPull)
{
    Bot bot;
    bot.group.StartPull(Self, Kobold);

    EXPECT_EQ(bot.Think(CommandState::Follow), "");
}

TEST(CommandStrategy, ABotDoesNotFollowAPlayerOutOfReach)
{
    Bot bot;
    bot.fake.SetDistance(Player, 500.0f);

    EXPECT_EQ(bot.Think(CommandState::Follow), "");
}

TEST(CommandStrategy, AStayingBotDoesNotFollow)
{
    Bot bot;

    EXPECT_EQ(bot.Think(CommandState::Stay), "");
}

TEST(ClassModules, TheWarriorTankHasTheCommandAndClassStrategies)
{
    StrategyStack stack = Bots::MakeStrategyStack(Bots::BotClass::Warrior, Bots::BotRole::Tank);

    EXPECT_EQ(stack.Names(), (std::vector<std::string>{"command", "warrior tank"}));
}

TEST(ClassModules, AClassAndRoleWithoutAModuleHaveOnlyTheCommandStrategy)
{
    StrategyStack stack = Bots::MakeStrategyStack(Bots::BotClass::Druid, Bots::BotRole::Healer);

    EXPECT_EQ(stack.Names(), (std::vector<std::string>{"command"}));
}
