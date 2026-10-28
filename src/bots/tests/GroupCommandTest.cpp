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

namespace
{
    // An online bot of no group, a player who may invite, and room in the group.
    Bots::GroupFacts FreeBot()
    {
        Bots::GroupFacts facts;
        facts.botKnown = true;
        facts.botOnline = true;
        facts.botInPlayerGroup = false;
        facts.botInOtherGroup = false;
        facts.playerMayInvite = true;
        facts.groupFull = false;
        facts.botOfOtherFaction = false;
        return facts;
    }
}

TEST(GroupCommandPlan, InviteLogsInAnOfflineBotFirst)
{
    Bots::GroupFacts facts = FreeBot();
    facts.botOnline = false;

    Bots::GroupCommandPlan plan = Bots::PlanGroupCommand(GroupCommandKind::Invite, facts);

    EXPECT_TRUE(plan.ok);
    EXPECT_TRUE(plan.login);
    // The rest waits for the bot to be in the world.
    EXPECT_FALSE(plan.addToGroup);
    EXPECT_FALSE(plan.teleport);
}

TEST(GroupCommandPlan, InviteAddsAnOnlineBotAndTeleportsIt)
{
    Bots::GroupCommandPlan plan = Bots::PlanGroupCommand(GroupCommandKind::Invite, FreeBot());

    EXPECT_TRUE(plan.ok);
    EXPECT_FALSE(plan.login);
    EXPECT_TRUE(plan.addToGroup);
    EXPECT_TRUE(plan.teleport);
    EXPECT_FALSE(plan.leaveGroup);
    EXPECT_FALSE(plan.logout);
}

TEST(GroupCommandPlan, InviteOfABotInTheGroupOnlyTeleportsIt)
{
    Bots::GroupFacts facts = FreeBot();
    facts.botInPlayerGroup = true;
    // A full group or a player who is no leader does not matter then.
    facts.groupFull = true;
    facts.playerMayInvite = false;

    Bots::GroupCommandPlan plan = Bots::PlanGroupCommand(GroupCommandKind::Invite, facts);

    EXPECT_TRUE(plan.ok);
    EXPECT_FALSE(plan.addToGroup);
    EXPECT_TRUE(plan.teleport);
}

TEST(GroupCommandPlan, InviteRefusesWithAFixedLine)
{
    Bots::GroupFacts unknown = FreeBot();
    unknown.botKnown = false;
    Bots::GroupFacts otherGroup = FreeBot();
    otherGroup.botInOtherGroup = true;
    Bots::GroupFacts full = FreeBot();
    full.groupFull = true;
    Bots::GroupFacts noLeader = FreeBot();
    noLeader.playerMayInvite = false;

    Bots::GroupCommandPlan plan = Bots::PlanGroupCommand(GroupCommandKind::Invite, unknown);
    EXPECT_FALSE(plan.ok);
    EXPECT_FALSE(plan.login);
    EXPECT_STREQ(plan.line, "No bot has that name.");
    EXPECT_STREQ(Bots::PlanGroupCommand(GroupCommandKind::Invite, otherGroup).line, "That bot is in another group.");
    EXPECT_STREQ(Bots::PlanGroupCommand(GroupCommandKind::Invite, full).line, "Your group is full.");
    EXPECT_STREQ(Bots::PlanGroupCommand(GroupCommandKind::Invite, noLeader).line, "Only the group leader can invite.");
}

TEST(GroupCommandPlan, DismissMakesTheBotLeaveAndLogOut)
{
    Bots::GroupFacts facts = FreeBot();
    facts.botInPlayerGroup = true;

    Bots::GroupCommandPlan plan = Bots::PlanGroupCommand(GroupCommandKind::Dismiss, facts);

    EXPECT_TRUE(plan.ok);
    EXPECT_TRUE(plan.leaveGroup);
    EXPECT_TRUE(plan.logout);
    EXPECT_FALSE(plan.login);
    EXPECT_FALSE(plan.teleport);
}

TEST(GroupCommandPlan, DismissOfAnOfflineBotInTheGroupOnlyRemovesIt)
{
    Bots::GroupFacts facts = FreeBot();
    facts.botOnline = false;
    facts.botInPlayerGroup = true;

    Bots::GroupCommandPlan plan = Bots::PlanGroupCommand(GroupCommandKind::Dismiss, facts);

    EXPECT_TRUE(plan.ok);
    EXPECT_TRUE(plan.leaveGroup);
    EXPECT_FALSE(plan.logout);
}

TEST(GroupCommandPlan, DismissRefusesABotOutsideTheGroup)
{
    Bots::GroupFacts unknown = FreeBot();
    unknown.botKnown = false;

    Bots::GroupCommandPlan plan = Bots::PlanGroupCommand(GroupCommandKind::Dismiss, FreeBot());
    EXPECT_FALSE(plan.ok);
    EXPECT_FALSE(plan.logout);
    EXPECT_STREQ(plan.line, "That bot is not in your group.");
    EXPECT_STREQ(Bots::PlanGroupCommand(GroupCommandKind::Dismiss, unknown).line, "No bot has that name.");
}

// jvbastel/WOWAI#199: a bot of the other faction.

TEST(GroupCommandPlan, InviteRefusesABotOfTheOtherFactionAndDoesNotLogItIn)
{
    Bots::GroupFacts facts = FreeBot();
    facts.botOnline = false;
    facts.botOfOtherFaction = true;

    Bots::GroupCommandPlan plan = Bots::PlanGroupCommand(GroupCommandKind::Invite, facts);

    EXPECT_FALSE(plan.ok);
    EXPECT_FALSE(plan.login);
    EXPECT_STREQ(plan.line, "That bot is of the other faction.");
}
