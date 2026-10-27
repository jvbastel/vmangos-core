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

TEST(CommandStrategy, AFollowingBotLootsACorpseBeforeItFollows)
{
    Bot bot;
    bot.fake.SetFollowTarget(Player);
    bot.fake.SetLootWork(true, true);

    EXPECT_EQ(bot.Think(CommandState::Follow), "loot a corpse");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"loot"}));
}

TEST(CommandStrategy, ABotFollowsAgainWhenItHasLooted)
{
    Bot bot;
    bot.fake.SetLootWork(true, true);
    bot.Think(CommandState::Follow);
    bot.fake.SetLootWork(false);

    EXPECT_EQ(bot.Think(CommandState::Follow), "follow the player");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"loot", "follow 0x0000000000000005"}));
}

TEST(CommandStrategy, ABotWithoutACommandLoots)
{
    Bot bot;
    bot.fake.SetLootWork(true, true);

    EXPECT_EQ(bot.Think(CommandState::None), "loot a corpse");
}

TEST(CommandStrategy, AStayingBotLootsOnlyInPlace)
{
    Bot bot;
    bot.fake.SetLootWork(true, true);
    EXPECT_EQ(bot.Think(CommandState::Stay), "");

    bot.fake.SetLootWork(true, false);
    EXPECT_EQ(bot.Think(CommandState::Stay), "loot a corpse");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"loot in place"}));
}

TEST(CommandStrategy, ABotDoesNotLootInAFight)
{
    Bot bot;
    bot.fake.SetLootWork(true);
    bot.fake.SetInCombat(Self);

    EXPECT_EQ(bot.Think(CommandState::Follow), "");
}

TEST(CommandStrategy, ThePullerDoesNotLootDuringItsPull)
{
    Bot bot;
    bot.fake.SetLootWork(true);
    bot.group.StartPull(Self, Kobold);

    EXPECT_EQ(bot.Think(CommandState::Follow), "");
}

// The test driver of a scripted run loots no corpses: a loot walk stops its
// scripted move (jvbastel/WOWAI#99). It still rolls on group loot, which the
// game layer does without a rule.
TEST(CommandStrategy, TheTestDriverOfAScriptedRunLootsNoCorpse)
{
    Bot bot;
    bot.fake.SetLootWork(true);
    BotContext context{bot.fake, bot.fake, bot.group, CommandState::None, Player};
    context.testDriver = true;

    EXPECT_EQ(bot.stack.Think(context), "");
    EXPECT_TRUE(bot.fake.Deeds().empty());
}

TEST(ClassModules, TheWarriorTankHasTheCommandRestGroupKeepOutClassAndDeathStrategies)
{
    StrategyStack stack = Bots::MakeStrategyStack(Bots::BotClass::Warrior, Bots::BotRole::Tank);

    EXPECT_EQ(stack.Names(), (std::vector<std::string>{"command", "rest", "group rules", "keep out", "warrior tank", "death"}));
}

TEST(ClassModules, AClassAndRoleWithoutAModuleHaveNoClassStrategy)
{
    StrategyStack stack = Bots::MakeStrategyStack(Bots::BotClass::Druid, Bots::BotRole::Healer);

    EXPECT_EQ(stack.Names(), (std::vector<std::string>{"command", "member spot", "rest", "group rules", "keep out", "death"}));
}

TEST(CommandStrategy, ABotThatWaitsItsStartDelayDoesNotFollowYet)
{
    Bot bot;
    BotContext context{bot.fake, bot.fake, bot.group, CommandState::Follow, Player};
    context.startWaits = true;

    EXPECT_EQ(bot.stack.Think(context), "");
    EXPECT_TRUE(bot.fake.Deeds().empty());
}

// The healer goes to a hurt member in a fight: the follow rule does not take
// it back to the player before the fight ends (WOWAI#93, Hela switched every
// second between the two).
TEST(CommandStrategy, ABotThatFollowsAMemberInTheGroupFightDoesNotFollowThePlayer)
{
    Bot bot;
    Bots::Guid const Tank = 0x12;
    bot.fake.SetHealth(Tank, 300, 600);
    bot.fake.SetGroupMembers({Tank, Player});
    bot.fake.SetFollowTarget(Tank);
    bot.fake.SetVictim(Kobold, Tank);
    bot.fake.SetInCombat(Tank);

    EXPECT_EQ(bot.Think(CommandState::Follow), "");
}

TEST(CommandStrategy, ABotThatFollowsAMemberFollowsThePlayerAfterTheFight)
{
    Bot bot;
    Bots::Guid const Tank = 0x12;
    bot.fake.SetHealth(Tank, 300, 600);
    bot.fake.SetGroupMembers({Tank, Player});
    bot.fake.SetFollowTarget(Tank);

    EXPECT_EQ(bot.Think(CommandState::Follow), "follow the player");
}
