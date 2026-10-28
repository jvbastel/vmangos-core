#include "BotPanel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using Bots::BotPanelMessages;
using Bots::BotRole;
using Bots::Faction;
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
            {"Shanka", BotRole::Melee, Faction::Horde, false},
            {"Hela", BotRole::Healer, Faction::Horde, false},
            {"Morzul", BotRole::Ranged, Faction::Horde, false},
            {"Grukk", BotRole::Tank, Faction::Horde, false},
            {"Ezzra", BotRole::Ranged, Faction::Horde, false},
            {"Nixie", BotRole::Ranged, Faction::Alliance, false},
            {"Aldric", BotRole::Tank, Faction::Alliance, false},
            {"Corwin", BotRole::Melee, Faction::Alliance, false},
            {"Fizzwick", BotRole::Ranged, Faction::Alliance, false},
            {"Brenna", BotRole::Healer, Faction::Alliance, false},
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

    EXPECT_EQ(NamesOf(PanelBotsFor(TheTenBots(), Faction::Horde)), expected);
}

TEST(BotPanel, AnAlliancePlayerSeesTheAllianceBotsByRole)
{
    std::vector<std::string> const expected = {"Aldric", "Brenna", "Corwin", "Fizzwick", "Nixie"};

    EXPECT_EQ(NamesOf(PanelBotsFor(TheTenBots(), Faction::Alliance)), expected);
}

TEST(BotPanel, LeavesOutTheWorldBots)
{
    std::vector<PanelBot> bots = TheTenBots();
    bots.push_back({"Wanderer", BotRole::Tank, Faction::Horde, true});

    std::vector<std::string> const names = NamesOf(PanelBotsFor(bots, Faction::Horde));

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
    std::vector<PanelBot> const listed = PanelBotsFor(TheTenBots(), Faction::Alliance);
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
