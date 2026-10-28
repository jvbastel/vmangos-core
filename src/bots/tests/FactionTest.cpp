#include "Faction.h"

#include <gtest/gtest.h>

using Bots::Faction;
using Bots::ParseFaction;

// Spec #188 fix-up (standards): one faction type and one parse of the faction
// words, for the gear templates (GearTemplate.h) and the bot recipes
// (BotRecipe.h).
TEST(Faction, ReadsTheTwoFactionWords)
{
    Faction faction = Faction::Horde;
    EXPECT_TRUE(ParseFaction("alliance", faction));
    EXPECT_EQ(faction, Faction::Alliance);
    EXPECT_TRUE(ParseFaction("horde", faction));
    EXPECT_EQ(faction, Faction::Horde);
}

TEST(Faction, AnotherWordIsNoFaction)
{
    Faction faction = Faction::Alliance;
    for (char const* word : {"", "Horde", "orc", "both", "alliances"})
    {
        EXPECT_FALSE(ParseFaction(word, faction)) << word;
        EXPECT_EQ(faction, Faction::Alliance) << word;
    }
}
