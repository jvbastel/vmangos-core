#include "GroupCommand.h"

#include <gtest/gtest.h>

using Bots::GroupCommandKind;
using Bots::ParseGroupCommand;
using Bots::ParsedGroupCommand;

TEST(GroupCommand, InviteNamesTheBot)
{
    ParsedGroupCommand parsed = ParseGroupCommand("invite Grukk");

    ASSERT_TRUE(parsed.ok);
    EXPECT_EQ(parsed.command.kind, GroupCommandKind::Invite);
    EXPECT_EQ(parsed.command.botName, "Grukk");
}

TEST(GroupCommand, DismissNamesTheBot)
{
    ParsedGroupCommand parsed = ParseGroupCommand("dismiss Zuljin");

    ASSERT_TRUE(parsed.ok);
    EXPECT_EQ(parsed.command.kind, GroupCommandKind::Dismiss);
    EXPECT_EQ(parsed.command.botName, "Zuljin");
}

TEST(GroupCommand, TheWordIgnoresCaseAndOuterSpaces)
{
    ParsedGroupCommand parsed = ParseGroupCommand("  INVITE   grukk ");

    ASSERT_TRUE(parsed.ok);
    EXPECT_EQ(parsed.command.kind, GroupCommandKind::Invite);
    EXPECT_EQ(parsed.command.botName, "grukk");
}

TEST(GroupCommand, TheWordAloneHasNoBotName)
{
    // In a whisper, it is for the whispered bot.
    ParsedGroupCommand parsed = ParseGroupCommand("dismiss");

    ASSERT_TRUE(parsed.ok);
    EXPECT_EQ(parsed.command.kind, GroupCommandKind::Dismiss);
    EXPECT_EQ(parsed.command.botName, "");
    EXPECT_TRUE(ParseGroupCommand("Invite").ok);
}

TEST(GroupCommand, OtherTextIsNoGroupCommand)
{
    EXPECT_FALSE(ParseGroupCommand("").ok);
    EXPECT_FALSE(ParseGroupCommand("follow").ok);
    EXPECT_FALSE(ParseGroupCommand("Grukk invite").ok);
    EXPECT_FALSE(ParseGroupCommand("invite Grukk now").ok);
    EXPECT_FALSE(ParseGroupCommand("please invite Grukk").ok);
    EXPECT_FALSE(ParseGroupCommand("invites Grukk").ok);
}
