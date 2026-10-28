#include "RoleChoice.h"

#include <gtest/gtest.h>

using Bots::ParseRoleChoiceWhisper;
using Bots::RoleChoice;
using Bots::RoleChoiceMessage;

// jvbastel/WOWAI#197, and the spec #188 fix-up (S4): the role choice add-on
// whispers "WOWAI_ROLE 1:<role>" to the player's own name, as the bot panel
// asks for its list (BotPanel.h): the 1.12 client sends a whisper also with
// no group, so the server knows the pick before the first invite.
namespace
{
    RoleChoiceMessage FromSelf(char const* text) { return ParseRoleChoiceWhisper("Tankard", "Tankard", text); }
}

TEST(RoleChoice, ReadsEachRole)
{
    EXPECT_EQ(FromSelf("WOWAI_ROLE 1:tank").choice, RoleChoice::Tank);
    EXPECT_EQ(FromSelf("WOWAI_ROLE 1:healer").choice, RoleChoice::Healer);
    EXPECT_EQ(FromSelf("WOWAI_ROLE 1:melee").choice, RoleChoice::Melee);
    EXPECT_EQ(FromSelf("WOWAI_ROLE 1:ranged").choice, RoleChoice::Ranged);
}

TEST(RoleChoice, AWhisperOfTheAddOnToTheSenderIsOurs)
{
    EXPECT_TRUE(FromSelf("WOWAI_ROLE 1:tank").ours);
    // The name of the sender in any case.
    EXPECT_TRUE(ParseRoleChoiceWhisper("Tankard", "tankard", "WOWAI_ROLE 1:tank").ours);
}

TEST(RoleChoice, AWhisperOfTheRoleToAnotherPlayerIsNotOurs)
{
    RoleChoiceMessage const message = ParseRoleChoiceWhisper("Tankard", "Hela", "WOWAI_ROLE 1:tank");

    EXPECT_FALSE(message.ours);
    EXPECT_EQ(message.choice, RoleChoice::None);
}

TEST(RoleChoice, NoneClearsThePick)
{
    RoleChoiceMessage const message = FromSelf("WOWAI_ROLE 1:none");

    EXPECT_TRUE(message.ours);
    EXPECT_EQ(message.choice, RoleChoice::None);
}

TEST(RoleChoice, AnUnknownVersionGivesNoChoice)
{
    RoleChoiceMessage const message = FromSelf("WOWAI_ROLE 2:tank");

    EXPECT_TRUE(message.ours);
    EXPECT_EQ(message.choice, RoleChoice::None);
}

TEST(RoleChoice, ABadMessageGivesNoChoice)
{
    for (char const* text : {"WOWAI_ROLE 1:", "WOWAI_ROLE 1:dps", "WOWAI_ROLE tank", "WOWAI_ROLE ", "WOWAI_ROLE 1:tank:x",
             "WOWAI_ROLE 1:Tank ", "WOWAI_ROLE 1tank"})
    {
        RoleChoiceMessage const message = FromSelf(text);
        EXPECT_TRUE(message.ours) << text;
        EXPECT_EQ(message.choice, RoleChoice::None) << text;
    }
}

TEST(RoleChoice, AWhisperOfAnotherKindIsNotOurs)
{
    for (char const* text : {"OTHER 1:tank", "WOWAI_ROLES 1:tank", "WOWAI_ROLE", "WOWAI_ROLE\t1:tank", "", "tank",
             "WOWAI_BOTS 1:list"})
    {
        RoleChoiceMessage const message = FromSelf(text);
        EXPECT_FALSE(message.ours) << text;
        EXPECT_EQ(message.choice, RoleChoice::None) << text;
    }
}
