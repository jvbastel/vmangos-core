#include "Strategy.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

#include <memory>

using Bots::BotContext;
using Bots::GroupCoordinator;
using Bots::Rule;
using Bots::Strategy;
using Bots::StrategyStack;
using Bots::Tests::FakeBot;

namespace
{
    Bots::Guid const Self = 0x11;
    Bots::Guid const Kobold = 0xF130000302000022;

    bool Always(BotContext const&) { return true; }
    bool Never(BotContext const&) { return false; }

    // A rule that casts `spell` on the kobold.
    Rule Cast(char const* name, int priority, Bots::SpellId spell, bool fires = true, bool canDo = true)
    {
        Rule rule;
        rule.name = name;
        rule.priority = priority;
        rule.trigger = fires ? Always : Never;
        rule.action.canDo = canDo ? Always : Never;
        rule.action.run = [spell](BotContext const& context) { context.actions.CastSpell(spell, Kobold); };
        return rule;
    }

    std::shared_ptr<Strategy const> MakeStrategy(char const* name, std::vector<Rule> rules)
    {
        return std::make_shared<Strategy const>(Strategy{name, std::move(rules)});
    }

    struct Bot
    {
        FakeBot fake{Self};
        GroupCoordinator group;
        BotContext context{fake, fake, group, Bots::CommandState::None, Bots::NoGuid};
    };
}

TEST(StrategyStack, DoesTheHighestPriorityActionWhoseTriggerFires)
{
    Bot bot;
    StrategyStack stack;
    stack.Push(MakeStrategy("warrior tank", {
        Cast("heroic strike", 10, 78),
        Cast("sunder armor", 30, 7386),
        Cast("taunt", 50, 355, false),
    }));

    EXPECT_EQ(stack.Think(bot.context), "sunder armor");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"cast 7386 on 0xf130000302000022"}));
}

TEST(StrategyStack, SkipsAnActionThatTheBotCannotDoNow)
{
    Bot bot;
    StrategyStack stack;
    stack.Push(MakeStrategy("warrior tank", {
        Cast("heroic strike", 10, 78),
        Cast("sunder armor", 30, 7386, true, false),
    }));

    EXPECT_EQ(stack.Think(bot.context), "heroic strike");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"cast 78 on 0xf130000302000022"}));
}

TEST(StrategyStack, DoesNothingWhenNoTriggerFires)
{
    Bot bot;
    StrategyStack stack;
    stack.Push(MakeStrategy("warrior tank", {Cast("taunt", 50, 355, false)}));

    EXPECT_EQ(stack.Think(bot.context), "");
    EXPECT_TRUE(bot.fake.Deeds().empty());
}

TEST(StrategyStack, ComparesTheRulesOfAllActiveStrategies)
{
    Bot bot;
    StrategyStack stack;
    stack.Push(MakeStrategy("command", {Cast("low", 10, 1)}));
    stack.Push(MakeStrategy("class", {Cast("middle", 20, 2)}));
    stack.Push(MakeStrategy("group", {Cast("high", 5, 3), Cast("highest", 40, 4)}));

    EXPECT_EQ(stack.Think(bot.context), "highest");
}

TEST(StrategyStack, OnATieTheStrategyHigherOnTheStackWins)
{
    Bot bot;
    StrategyStack stack;
    stack.Push(MakeStrategy("class", {Cast("class rule", 20, 1)}));
    stack.Push(MakeStrategy("boss reaction", {Cast("boss rule", 20, 2)}));

    EXPECT_EQ(stack.Think(bot.context), "boss rule");
}

TEST(StrategyStack, OnATieInOneStrategyTheFirstRuleWins)
{
    Bot bot;
    StrategyStack stack;
    stack.Push(MakeStrategy("class", {Cast("first", 20, 1), Cast("second", 20, 2)}));

    EXPECT_EQ(stack.Think(bot.context), "first");
}

TEST(StrategyStack, PushesAndRemovesStrategiesByName)
{
    Bot bot;
    StrategyStack stack;
    stack.Push(MakeStrategy("class", {Cast("class rule", 20, 1)}));
    stack.Push(MakeStrategy("boss reaction", {Cast("boss rule", 90, 2)}));
    EXPECT_TRUE(stack.Contains("boss reaction"));
    EXPECT_EQ(stack.Names(), (std::vector<std::string>{"class", "boss reaction"}));

    EXPECT_TRUE(stack.Remove("boss reaction"));

    EXPECT_FALSE(stack.Contains("boss reaction"));
    EXPECT_FALSE(stack.Remove("boss reaction"));
    EXPECT_EQ(stack.Think(bot.context), "class rule");
}
