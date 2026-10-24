#include "CommandState.h"

#include <gtest/gtest.h>

using Bots::ApplyBotCommand;
using Bots::BotCommandKind;
using Bots::CommandFacts;
using Bots::CommandOutcome;
using Bots::CommandState;

namespace
{
    CommandFacts const Ready = {true, true};
}

TEST(CommandState, FollowMakesTheBotFollow)
{
    CommandOutcome outcome = ApplyBotCommand(CommandState::None, BotCommandKind::Follow, Ready);

    EXPECT_TRUE(outcome.obeys);
    EXPECT_EQ(outcome.state, CommandState::Follow);
    EXPECT_STREQ(outcome.line, "");
}

TEST(CommandState, StayAndComeSetTheirState)
{
    EXPECT_EQ(ApplyBotCommand(CommandState::Follow, BotCommandKind::Stay, Ready).state, CommandState::Stay);
    EXPECT_EQ(ApplyBotCommand(CommandState::Stay, BotCommandKind::Come, Ready).state, CommandState::Come);
}

TEST(CommandState, StopClearsTheState)
{
    CommandOutcome outcome = ApplyBotCommand(CommandState::Follow, BotCommandKind::Stop, Ready);

    EXPECT_TRUE(outcome.obeys);
    EXPECT_EQ(outcome.state, CommandState::None);
}

TEST(CommandState, ADeadBotCannotMoveAndKeepsItsState)
{
    CommandFacts const dead = {false, true};
    for (BotCommandKind kind : {BotCommandKind::Follow, BotCommandKind::Stay, BotCommandKind::Come})
    {
        CommandOutcome outcome = ApplyBotCommand(CommandState::Follow, kind, dead);

        EXPECT_FALSE(outcome.obeys);
        EXPECT_EQ(outcome.state, CommandState::Follow);
        EXPECT_STREQ(outcome.line, "I am dead.");
    }
}

TEST(CommandState, ADeadBotStillStops)
{
    CommandOutcome outcome = ApplyBotCommand(CommandState::Follow, BotCommandKind::Stop, {false, false});

    EXPECT_TRUE(outcome.obeys);
    EXPECT_EQ(outcome.state, CommandState::None);
}

TEST(CommandState, ABotCannotFollowOrComeToAPlayerOutOfReach)
{
    CommandFacts const far = {true, false};
    for (BotCommandKind kind : {BotCommandKind::Follow, BotCommandKind::Come})
    {
        CommandOutcome outcome = ApplyBotCommand(CommandState::Stay, kind, far);

        EXPECT_FALSE(outcome.obeys);
        EXPECT_EQ(outcome.state, CommandState::Stay);
        EXPECT_STREQ(outcome.line, "I cannot reach you.");
    }
}

TEST(CommandState, ABotStaysWhenThePlayerIsOutOfReach)
{
    EXPECT_TRUE(ApplyBotCommand(CommandState::Follow, BotCommandKind::Stay, {true, false}).obeys);
}

namespace
{
    CommandFacts Tank(Bots::PullTarget target)
    {
        CommandFacts facts = Ready;
        facts.isTank = true;
        facts.playerTarget = target;
        return facts;
    }
}

TEST(CommandState, TheTankPullsAnEnemyTargetAndKeepsItsState)
{
    CommandOutcome outcome = ApplyBotCommand(CommandState::Follow, BotCommandKind::Pull, Tank(Bots::PullTarget::Enemy));

    EXPECT_TRUE(outcome.obeys);
    EXPECT_EQ(outcome.state, CommandState::Follow);
    EXPECT_STREQ(outcome.line, "");
}

TEST(CommandState, PullWithNoTargetGivesTheFixedLine)
{
    CommandOutcome outcome = ApplyBotCommand(CommandState::Follow, BotCommandKind::Pull, Tank(Bots::PullTarget::None));

    EXPECT_FALSE(outcome.obeys);
    EXPECT_EQ(outcome.state, CommandState::Follow);
    EXPECT_STREQ(outcome.line, "You have no target.");
}

TEST(CommandState, TheTankDoesNotPullAFriend)
{
    CommandOutcome outcome = ApplyBotCommand(CommandState::None, BotCommandKind::Pull, Tank(Bots::PullTarget::NotEnemy));

    EXPECT_FALSE(outcome.obeys);
    EXPECT_STREQ(outcome.line, "I cannot pull that.");
}

TEST(CommandState, ADeadTankDoesNotPull)
{
    CommandFacts facts = Tank(Bots::PullTarget::Enemy);
    facts.botAlive = false;

    EXPECT_STREQ(ApplyBotCommand(CommandState::None, BotCommandKind::Pull, facts).line, "I am dead.");
}

TEST(CommandState, OnlyTheTankPullsAndTheOtherBotsSayNothing)
{
    for (Bots::PullTarget target : {Bots::PullTarget::Enemy, Bots::PullTarget::None})
    {
        CommandFacts facts = Tank(target);
        facts.isTank = false;

        CommandOutcome outcome = ApplyBotCommand(CommandState::Follow, BotCommandKind::Pull, facts);

        EXPECT_FALSE(outcome.obeys);
        EXPECT_EQ(outcome.state, CommandState::Follow);
        EXPECT_STREQ(outcome.line, "");
    }
}

TEST(CommandState, TheTankDoesNotPullWhileTheGroupRests)
{
    CommandFacts facts = Tank(Bots::PullTarget::Enemy);
    facts.groupRests = true;

    CommandOutcome outcome = ApplyBotCommand(CommandState::Follow, BotCommandKind::Pull, facts);

    EXPECT_FALSE(outcome.obeys);
    EXPECT_STREQ(outcome.line, "We are resting.");
}

namespace
{
    CommandFacts DamageBot(Bots::PullTarget target)
    {
        CommandFacts facts = Ready;
        facts.isDamage = true;
        facts.playerTarget = target;
        return facts;
    }
}

TEST(CommandState, ADamageBotAttacksAnEnemyTargetAndKeepsItsState)
{
    CommandOutcome outcome = ApplyBotCommand(CommandState::Follow, BotCommandKind::Attack, DamageBot(Bots::PullTarget::Enemy));

    EXPECT_TRUE(outcome.obeys);
    EXPECT_EQ(outcome.state, CommandState::Follow);
    EXPECT_STREQ(outcome.line, "");
}

TEST(CommandState, AttackWithNoTargetOrAFriendGivesAFixedLine)
{
    EXPECT_STREQ(ApplyBotCommand(CommandState::Follow, BotCommandKind::Attack, DamageBot(Bots::PullTarget::None)).line,
        "You have no target.");
    CommandOutcome outcome = ApplyBotCommand(CommandState::Follow, BotCommandKind::Attack, DamageBot(Bots::PullTarget::NotEnemy));
    EXPECT_FALSE(outcome.obeys);
    EXPECT_STREQ(outcome.line, "I cannot attack that.");
}

TEST(CommandState, ADeadDamageBotDoesNotAttack)
{
    CommandFacts facts = DamageBot(Bots::PullTarget::Enemy);
    facts.botAlive = false;

    EXPECT_STREQ(ApplyBotCommand(CommandState::None, BotCommandKind::Attack, facts).line, "I am dead.");
}

TEST(CommandState, TheTankAndTheHealerIgnoreAttackAndSayNothing)
{
    CommandFacts facts = DamageBot(Bots::PullTarget::Enemy);
    facts.isDamage = false;

    CommandOutcome outcome = ApplyBotCommand(CommandState::Follow, BotCommandKind::Attack, facts);

    EXPECT_FALSE(outcome.obeys);
    EXPECT_EQ(outcome.state, CommandState::Follow);
    EXPECT_STREQ(outcome.line, "");
}
