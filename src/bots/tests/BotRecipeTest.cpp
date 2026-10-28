#include "BotRecipe.h"

#include <gtest/gtest.h>

using Bots::BotClass;
using Bots::BotRace;
using Bots::BotRecipeResult;
using Bots::BotRole;
using Bots::ParseBotRecipe;

TEST(BotRecipe, ReadsNameClassAndRoleOfALevel16Bot)
{
    BotRecipeResult result = ParseBotRecipe({"Grukk", "warrior", "tank"});

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.recipe.name, "Grukk");
    EXPECT_EQ(result.recipe.botClass, BotClass::Warrior);
    EXPECT_EQ(result.recipe.role, BotRole::Tank);
    EXPECT_EQ(result.recipe.level, 16u);
}

TEST(BotRecipe, EachVersion1ClassHasItsHordeRace)
{
    EXPECT_EQ(ParseBotRecipe({"Grukk", "warrior", "tank"}).recipe.race, BotRace::Orc);
    EXPECT_EQ(ParseBotRecipe({"Zuljin", "priest", "healer"}).recipe.race, BotRace::Troll);
    EXPECT_EQ(ParseBotRecipe({"Shank", "rogue", "melee"}).recipe.race, BotRace::Orc);
    EXPECT_EQ(ParseBotRecipe({"Morbid", "warlock", "ranged"}).recipe.race, BotRace::Undead);
}

TEST(BotRecipe, ARaceWordChoosesAnotherHordeRace)
{
    BotRecipeResult result = ParseBotRecipe({"Moo", "warrior", "tank", "tauren"});

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.recipe.race, BotRace::Tauren);
}

TEST(BotRecipe, WordsIgnoreCase)
{
    BotRecipeResult result = ParseBotRecipe({"Moo", "Warrior", "TANK", "Tauren"});

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.recipe.botClass, BotClass::Warrior);
    EXPECT_EQ(result.recipe.role, BotRole::Tank);
    EXPECT_EQ(result.recipe.race, BotRace::Tauren);
}

TEST(BotRecipe, RefusesARaceThatCannotHaveTheClass)
{
    BotRecipeResult result = ParseBotRecipe({"Grukk", "priest", "healer", "orc"});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "An orc cannot be a priest.");
}

TEST(BotRecipe, RefusesAnUnknownRace)
{
    BotRecipeResult result = ParseBotRecipe({"Anduin", "priest", "healer", "elf"});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "Unknown race 'elf'. Use orc, undead, tauren, troll, human, dwarf, nightelf, gnome, "
        "horde, or alliance.");
}

TEST(BotRecipe, RefusesARoleThatTheClassCannotPlay)
{
    BotRecipeResult result = ParseBotRecipe({"Morbid", "warlock", "tank"});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "A warlock cannot play the tank role.");
}

TEST(BotRecipe, RefusesAnUnknownClassOrRole)
{
    EXPECT_EQ(ParseBotRecipe({"Grukk", "paladin", "tank"}).error,
        "Unknown class 'paladin'. Use warrior, priest, rogue, warlock, mage, hunter, shaman, or druid.");
    EXPECT_EQ(ParseBotRecipe({"Grukk", "warrior", "dps"}).error,
        "Unknown role 'dps'. Use tank, healer, melee, or ranged.");
}

TEST(BotRecipe, RefusesTooFewOrTooManyWords)
{
    std::string const usage = "Usage: <name> <class> <role> [race] [gear <template>]";

    EXPECT_EQ(ParseBotRecipe({"Grukk", "warrior"}).error, usage);
    EXPECT_EQ(ParseBotRecipe({"Grukk", "warrior", "tank", "orc", "extra"}).error, usage);
    EXPECT_EQ(ParseBotRecipe({"Grukk", "warrior", "tank", "gear"}).error, usage);
    EXPECT_EQ(ParseBotRecipe({"Grukk", "warrior", "tank", "orc", "gear", "a", "b"}).error, usage);
}

TEST(BotRecipe, WithNoGearWordTheBotWearsTheGeneratedTemplate)
{
    EXPECT_EQ(ParseBotRecipe({"Grukk", "warrior", "tank"}).recipe.gearTemplate, "");
    EXPECT_EQ(ParseBotRecipe({"Grukk", "warrior", "tank", "tauren"}).recipe.gearTemplate, "");
}

TEST(BotRecipe, TheGearWordNamesATemplate)
{
    BotRecipeResult result = ParseBotRecipe({"Grukk", "warrior", "tank", "GEAR", "Grukk_Dungeon"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.recipe.gearTemplate, "grukk_dungeon");
    EXPECT_EQ(result.recipe.race, BotRace::Orc);

    result = ParseBotRecipe({"Grukk", "warrior", "tank", "tauren", "gear", "grukk_dungeon"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.recipe.gearTemplate, "grukk_dungeon");
    EXPECT_EQ(result.recipe.race, BotRace::Tauren);
}

TEST(BotRecipe, RoleNamesAreTheWordsOfTheCommand)
{
    EXPECT_STREQ(Bots::RoleName(BotRole::Tank), "tank");
    EXPECT_STREQ(Bots::RoleName(BotRole::Healer), "healer");
    EXPECT_STREQ(Bots::RoleName(BotRole::Melee), "melee");
    EXPECT_STREQ(Bots::RoleName(BotRole::Ranged), "ranged");
}

// jvbastel/WOWAI#199: the Alliance races.

TEST(BotRecipe, ReadsEachAllianceRace)
{
    EXPECT_EQ(ParseBotRecipe({"Aldric", "warrior", "tank", "human"}).recipe.race, BotRace::Human);
    EXPECT_EQ(ParseBotRecipe({"Brenna", "priest", "healer", "Dwarf"}).recipe.race, BotRace::Dwarf);
    EXPECT_EQ(ParseBotRecipe({"Corwin", "rogue", "melee", "nightelf"}).recipe.race, BotRace::NightElf);
    EXPECT_EQ(ParseBotRecipe({"Fizzwick", "mage", "ranged", "GNOME"}).recipe.race, BotRace::Gnome);
}

TEST(BotRecipe, TheAllianceWordGivesEachClassItsAllianceRace)
{
    EXPECT_EQ(ParseBotRecipe({"Aldric", "warrior", "tank", "alliance"}).recipe.race, BotRace::Human);
    EXPECT_EQ(ParseBotRecipe({"Brenna", "priest", "healer", "alliance"}).recipe.race, BotRace::Dwarf);
    EXPECT_EQ(ParseBotRecipe({"Corwin", "rogue", "melee", "alliance"}).recipe.race, BotRace::Human);
    EXPECT_EQ(ParseBotRecipe({"Fizzwick", "mage", "ranged", "alliance"}).recipe.race, BotRace::Gnome);
    EXPECT_EQ(ParseBotRecipe({"Nixie", "warlock", "ranged", "Alliance"}).recipe.race, BotRace::Gnome);
    EXPECT_EQ(ParseBotRecipe({"Bolt", "hunter", "ranged", "alliance"}).recipe.race, BotRace::Dwarf);
    EXPECT_EQ(ParseBotRecipe({"Leaf", "druid", "healer", "alliance"}).recipe.race, BotRace::NightElf);
}

TEST(BotRecipe, TheHordeWordGivesTheHordeRace)
{
    BotRecipeResult result = ParseBotRecipe({"Grukk", "warrior", "tank", "horde", "gear", "grukk_dungeon"});

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.recipe.race, BotRace::Orc);
    EXPECT_EQ(result.recipe.gearTemplate, "grukk_dungeon");
}

TEST(BotRecipe, RefusesAnAllianceRaceThatCannotHaveTheClass)
{
    EXPECT_EQ(ParseBotRecipe({"Brenna", "priest", "healer", "gnome"}).error, "A gnome cannot be a priest.");
    EXPECT_EQ(ParseBotRecipe({"Fizzwick", "mage", "ranged", "dwarf"}).error, "A dwarf cannot be a mage.");
    EXPECT_EQ(ParseBotRecipe({"Nixie", "warlock", "ranged", "nightelf"}).error, "A nightelf cannot be a warlock.");
    EXPECT_EQ(ParseBotRecipe({"Leaf", "druid", "healer", "human"}).error, "A human cannot be a druid.");
}

TEST(BotRecipe, RefusesAClassThatTheFactionDoesNotHave)
{
    EXPECT_EQ(ParseBotRecipe({"Thrall", "shaman", "healer", "alliance"}).error, "The alliance has no shaman.");
}
