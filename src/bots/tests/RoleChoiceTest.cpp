#include "RoleChoice.h"

#include <gtest/gtest.h>

using Bots::ParseRoleChoiceMessage;
using Bots::RoleChoice;
using Bots::RoleChoiceMessage;

// jvbastel/WOWAI#197: the role choice add-on sends "WOWAI_ROLE" as the add-on
// prefix and "1:<role>" as the text. The 1.12 client puts a tab between them
// in the party chat message that the server gets.
TEST(RoleChoice, ReadsEachRole)
{
    EXPECT_EQ(ParseRoleChoiceMessage("WOWAI_ROLE\t1:tank").choice, RoleChoice::Tank);
    EXPECT_EQ(ParseRoleChoiceMessage("WOWAI_ROLE\t1:healer").choice, RoleChoice::Healer);
    EXPECT_EQ(ParseRoleChoiceMessage("WOWAI_ROLE\t1:melee").choice, RoleChoice::Melee);
    EXPECT_EQ(ParseRoleChoiceMessage("WOWAI_ROLE\t1:ranged").choice, RoleChoice::Ranged);
}

TEST(RoleChoice, AMessageOfTheAddOnIsOurs)
{
    RoleChoiceMessage const message = ParseRoleChoiceMessage("WOWAI_ROLE\t1:tank");

    EXPECT_TRUE(message.ours);
}

TEST(RoleChoice, NoneClearsThePick)
{
    RoleChoiceMessage const message = ParseRoleChoiceMessage("WOWAI_ROLE\t1:none");

    EXPECT_TRUE(message.ours);
    EXPECT_EQ(message.choice, RoleChoice::None);
}

TEST(RoleChoice, AnUnknownVersionGivesNoChoice)
{
    RoleChoiceMessage const message = ParseRoleChoiceMessage("WOWAI_ROLE\t2:tank");

    EXPECT_TRUE(message.ours);
    EXPECT_EQ(message.choice, RoleChoice::None);
}

TEST(RoleChoice, ABadMessageGivesNoChoice)
{
    for (char const* text : {"WOWAI_ROLE\t1:", "WOWAI_ROLE\t1:dps", "WOWAI_ROLE\ttank", "WOWAI_ROLE\t", "WOWAI_ROLE\t1:tank:x",
             "WOWAI_ROLE\t1:Tank ", "WOWAI_ROLE\t1tank"})
    {
        RoleChoiceMessage const message = ParseRoleChoiceMessage(text);
        EXPECT_TRUE(message.ours) << text;
        EXPECT_EQ(message.choice, RoleChoice::None) << text;
    }
}

TEST(RoleChoice, AMessageOfAnotherAddOnIsNotOurs)
{
    for (char const* text : {"OTHER\t1:tank", "WOWAI_ROLES\t1:tank", "WOWAI_ROLE", "WOWAI_ROLE 1:tank", "", "tank"})
    {
        RoleChoiceMessage const message = ParseRoleChoiceMessage(text);
        EXPECT_FALSE(message.ours) << text;
        EXPECT_EQ(message.choice, RoleChoice::None) << text;
    }
}
