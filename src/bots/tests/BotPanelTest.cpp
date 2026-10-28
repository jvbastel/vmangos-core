#include "BotPanel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using Bots::BotPanelMessages;
using Bots::BotRole;
using Bots::GearFaction;
using Bots::IsBotPanelRequest;
using Bots::PanelBot;
using Bots::PanelBotsFor;

// jvbastel/WOWAI#198: the bot panel of the role choice add-on lists the
// group bots of the player's faction, by role.
namespace
{
    std::vector<PanelBot> TheTenBots()
    {
        return {
            {"Shanka", BotRole::Melee, GearFaction::Horde, false},
            {"Hela", BotRole::Healer, GearFaction::Horde, false},
            {"Morzul", BotRole::Ranged, GearFaction::Horde, false},
            {"Grukk", BotRole::Tank, GearFaction::Horde, false},
            {"Ezzra", BotRole::Ranged, GearFaction::Horde, false},
            {"Nixie", BotRole::Ranged, GearFaction::Alliance, false},
            {"Aldric", BotRole::Tank, GearFaction::Alliance, false},
            {"Corwin", BotRole::Melee, GearFaction::Alliance, false},
            {"Fizzwick", BotRole::Ranged, GearFaction::Alliance, false},
            {"Brenna", BotRole::Healer, GearFaction::Alliance, false},
        };
    }

    std::vector<std::string> NamesOf(std::vector<PanelBot> const& bots)
    {
        std::vector<std::string> names;
        for (PanelBot const& bot : bots)
            names.push_back(bot.name);
        return names;
    }
}

TEST(BotPanel, AHordePlayerSeesTheHordeBotsByRole)
{
    std::vector<std::string> const expected = {"Grukk", "Hela", "Shanka", "Ezzra", "Morzul"};

    EXPECT_EQ(NamesOf(PanelBotsFor(TheTenBots(), GearFaction::Horde)), expected);
}

TEST(BotPanel, AnAlliancePlayerSeesTheAllianceBotsByRole)
{
    std::vector<std::string> const expected = {"Aldric", "Brenna", "Corwin", "Fizzwick", "Nixie"};

    EXPECT_EQ(NamesOf(PanelBotsFor(TheTenBots(), GearFaction::Alliance)), expected);
}

TEST(BotPanel, LeavesOutTheWorldBots)
{
    std::vector<PanelBot> bots = TheTenBots();
    bots.push_back({"Wanderer", BotRole::Tank, GearFaction::Horde, true});

    std::vector<std::string> const names = NamesOf(PanelBotsFor(bots, GearFaction::Horde));

    EXPECT_EQ(names.size(), 5u);
    EXPECT_EQ(names.front(), "Grukk");
}

// The add-on asks with a whisper to the player's own name: the 1.12 client
// sends no party add-on message without a group, and the panel is mostly
// used with no group.
TEST(BotPanel, ReadsTheListRequestOfTheAddOnAsAWhisperToTheSender)
{
    EXPECT_TRUE(IsBotPanelRequest("Thrallson", "Thrallson", "WOWAI_BOTS 1:list"));
}

TEST(BotPanel, TheNameOfTheSenderMatchesInAnyCase)
{
    EXPECT_TRUE(IsBotPanelRequest("Thrallson", "thrallson", "WOWAI_BOTS 1:list"));
}

TEST(BotPanel, AWhisperOfTheRequestToAnotherPlayerIsNoRequest)
{
    EXPECT_FALSE(IsBotPanelRequest("Thrallson", "Grukk", "WOWAI_BOTS 1:list"));
}

TEST(BotPanel, IgnoresOtherMessages)
{
    EXPECT_FALSE(IsBotPanelRequest("Thrallson", "Thrallson", "WOWAI_ROLE 1:tank"));
    EXPECT_FALSE(IsBotPanelRequest("Thrallson", "Thrallson", "WOWAI_BOTS 2:list"));
    EXPECT_FALSE(IsBotPanelRequest("Thrallson", "Thrallson", "WOWAI_BOTS 1:clear"));
    EXPECT_FALSE(IsBotPanelRequest("Thrallson", "Thrallson", "WOWAI_BOTS1:list"));
    EXPECT_FALSE(IsBotPanelRequest("Thrallson", "Thrallson", "hello"));
}

// The answer is system messages to the player alone: the client gives them
// to the add-on (CHAT_MSG_SYSTEM) also with no group, and the add-on hides
// them from the chat frames.
TEST(BotPanel, TheAnswerClearsThePanelThenSendsOneMessagePerBot)
{
    std::vector<PanelBot> const listed = PanelBotsFor(TheTenBots(), GearFaction::Alliance);
    std::vector<std::string> const expected = {
        "WOWAI_BOTS 1:clear",
        "WOWAI_BOTS 1:bot:tank:Aldric",
        "WOWAI_BOTS 1:bot:healer:Brenna",
        "WOWAI_BOTS 1:bot:melee:Corwin",
        "WOWAI_BOTS 1:bot:ranged:Fizzwick",
        "WOWAI_BOTS 1:bot:ranged:Nixie",
        "WOWAI_BOTS 1:end",
    };

    EXPECT_EQ(BotPanelMessages(listed), expected);
}

TEST(BotPanel, AnEmptyPanelStillGetsClearAndEnd)
{
    std::vector<std::string> const expected = {"WOWAI_BOTS 1:clear", "WOWAI_BOTS 1:end"};

    EXPECT_EQ(BotPanelMessages({}), expected);
}
