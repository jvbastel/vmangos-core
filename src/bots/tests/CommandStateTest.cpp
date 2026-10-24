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
