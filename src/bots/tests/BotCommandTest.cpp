#include "BotCommand.h"

#include <gtest/gtest.h>

using Bots::BotCommandKind;
using Bots::ParseBotCommand;
using Bots::ParsedBotCommand;

namespace
{
    std::vector<std::string> const GroupBots = {"Grukk", "Zuljin"};
}

TEST(BotCommand, FollowIsACommandToAllBots)
{
    ParsedBotCommand parsed = ParseBotCommand("follow", GroupBots);

    ASSERT_TRUE(parsed.ok);
    EXPECT_EQ(parsed.command.kind, BotCommandKind::Follow);
    EXPECT_EQ(parsed.command.botName, "");
}

TEST(BotCommand, EachCommandWordHasItsKind)
{
    EXPECT_EQ(ParseBotCommand("follow", GroupBots).command.kind, BotCommandKind::Follow);
    EXPECT_EQ(ParseBotCommand("stay", GroupBots).command.kind, BotCommandKind::Stay);
    EXPECT_EQ(ParseBotCommand("come", GroupBots).command.kind, BotCommandKind::Come);
    EXPECT_EQ(ParseBotCommand("stop", GroupBots).command.kind, BotCommandKind::Stop);
    EXPECT_TRUE(ParseBotCommand("stay", GroupBots).ok);
    EXPECT_TRUE(ParseBotCommand("come", GroupBots).ok);
    EXPECT_TRUE(ParseBotCommand("stop", GroupBots).ok);
}

TEST(BotCommand, ABotNameFirstAddressesThatBotOnly)
{
    ParsedBotCommand parsed = ParseBotCommand("zuljin stay", GroupBots);

    ASSERT_TRUE(parsed.ok);
    EXPECT_EQ(parsed.command.kind, BotCommandKind::Stay);
    EXPECT_EQ(parsed.command.botName, "Zuljin");
}

TEST(BotCommand, WordsIgnoreCaseAndOuterSpaces)
{
    ParsedBotCommand parsed = ParseBotCommand("  GRUKK   Come ", GroupBots);

    ASSERT_TRUE(parsed.ok);
    EXPECT_EQ(parsed.command.kind, BotCommandKind::Come);
    EXPECT_EQ(parsed.command.botName, "Grukk");
}

TEST(BotCommand, ANameThatIsNoBotOfTheGroupMakesNoCommand)
{
    EXPECT_FALSE(ParseBotCommand("Thrall follow", GroupBots).ok);
    EXPECT_FALSE(ParseBotCommand("Grukk follow", {}).ok);
}

TEST(BotCommand, OtherTextIsNoCommand)
{
    EXPECT_FALSE(ParseBotCommand("", GroupBots).ok);
    EXPECT_FALSE(ParseBotCommand("hello there", GroupBots).ok);
    EXPECT_FALSE(ParseBotCommand("follow me", GroupBots).ok);
    EXPECT_FALSE(ParseBotCommand("please follow", GroupBots).ok);
    EXPECT_FALSE(ParseBotCommand("Grukk follow now", GroupBots).ok);
    EXPECT_FALSE(ParseBotCommand("Grukk", GroupBots).ok);
    EXPECT_FALSE(ParseBotCommand("follow!", GroupBots).ok);
}

TEST(BotCommand, EachKindHasItsWord)
{
    EXPECT_STREQ(Bots::BotCommandWord(BotCommandKind::Follow), "follow");
    EXPECT_STREQ(Bots::BotCommandWord(BotCommandKind::Stop), "stop");
}
