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
    std::string const usage = "Usage: <name> <class> <role> [race|horde|alliance] [gear <template>] [hardcore [level]]";

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

// jvbastel/WOWAI#209: with the Forever rules on, the player plays the Warrior, so no bot is a Warrior.
TEST(BotRecipe, TheForeverRulesRefuseAWarriorBot)
{
    EXPECT_EQ(Bots::BotClassRefusal(BotClass::Warrior, true),
        "No Warrior bot: the Forever rules are on, and the player plays the Warrior.");
}

TEST(BotRecipe, TheForeverRulesAllowEachOtherClass)
{
    for (BotClass botClass : {BotClass::Priest, BotClass::Rogue, BotClass::Warlock, BotClass::Mage,
             BotClass::Hunter, BotClass::Shaman, BotClass::Druid})
        EXPECT_EQ(Bots::BotClassRefusal(botClass, true), "") << Bots::ClassName(botClass);
}

TEST(BotRecipe, WithoutTheForeverRulesAWarriorBotIsAllowed)
{
    EXPECT_EQ(Bots::BotClassRefusal(BotClass::Warrior, false), "");
}

// jvbastel/WOWAI#257: hardcore bots for a hardcore character's group.

TEST(BotRecipe, ABotIsNotHardcoreWithoutTheHardcoreWord)
{
    BotRecipeResult result = ParseBotRecipe({"Tankbot", "warrior", "tank", "gear", "tank_dungeon"});

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_FALSE(result.recipe.hardcore);
    EXPECT_EQ(result.recipe.level, 16u);
}

TEST(BotRecipe, TheHardcoreWordAloneAsksForTheLevelOfThePlayer)
{
    BotRecipeResult result = ParseBotRecipe({"Healbot", "priest", "healer", "HARDCORE"});

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(result.recipe.hardcore);
    EXPECT_EQ(result.recipe.level, Bots::PlayerLevel);
}

TEST(BotRecipe, TheHardcoreWordTakesALevel)
{
    BotRecipeResult result = ParseBotRecipe({"Rangebot", "mage", "ranged", "undead", "gear", "mage_dungeon", "hardcore", "7"});

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(result.recipe.hardcore);
    EXPECT_EQ(result.recipe.level, 7u);
    EXPECT_EQ(result.recipe.race, BotRace::Undead);
    EXPECT_EQ(result.recipe.gearTemplate, "mage_dungeon");
}

TEST(BotRecipe, RefusesAHardcoreLevelThatIsNoLevel)
{
    std::string const level = "A bot level is a number from 1 to 60.";

    EXPECT_EQ(ParseBotRecipe({"Tankbot", "warrior", "tank", "hardcore", "0"}).error, level);
    EXPECT_EQ(ParseBotRecipe({"Tankbot", "warrior", "tank", "hardcore", "61"}).error, level);
    EXPECT_EQ(ParseBotRecipe({"Tankbot", "warrior", "tank", "hardcore", "ten"}).error, level);
    EXPECT_EQ(ParseBotRecipe({"Tankbot", "warrior", "tank", "hardcore", "-3"}).error, level);
}

TEST(BotRecipe, TheHardcoreWordsComeLast)
{
    std::string const usage = "Usage: <name> <class> <role> [race|horde|alliance] [gear <template>] [hardcore [level]]";

    EXPECT_EQ(ParseBotRecipe({"Tankbot", "warrior", "tank", "hardcore", "5", "orc"}).error, usage);
    EXPECT_EQ(ParseBotRecipe({"Tankbot", "warrior", "tank", "hardcore", "gear", "tank_dungeon"}).error, usage);
}

TEST(HardcoreBotLevel, ABotAtThePlayersLevelOrBelowIsAllowed)
{
    Bots::HardcoreAsker const player = {true, true, 12};

    Bots::BotLevelChoice same = Bots::HardcoreBotLevel(12, player);
    ASSERT_TRUE(same.ok) << same.error;
    EXPECT_EQ(same.level, 12u);

    Bots::BotLevelChoice below = Bots::HardcoreBotLevel(9, player);
    ASSERT_TRUE(below.ok) << below.error;
    EXPECT_EQ(below.level, 9u);
}

TEST(HardcoreBotLevel, WithNoLevelTheBotGetsThePlayersLevel)
{
    Bots::BotLevelChoice choice = Bots::HardcoreBotLevel(Bots::PlayerLevel, {true, true, 23});

    ASSERT_TRUE(choice.ok) << choice.error;
    EXPECT_EQ(choice.level, 23u);
}

TEST(HardcoreBotLevel, RefusesALevelAboveThePlayersLevel)
{
    Bots::BotLevelChoice choice = Bots::HardcoreBotLevel(13, {true, true, 12});

    EXPECT_FALSE(choice.ok);
    EXPECT_EQ(choice.error, "A hardcore bot cannot be above your level (12).");
}

TEST(HardcoreBotLevel, OnlyAHardcorePlayerInTheGameGetsAHardcoreBot)
{
    Bots::BotLevelChoice console = Bots::HardcoreBotLevel(5, {false, false, 0});
    EXPECT_FALSE(console.ok);
    EXPECT_EQ(console.error, "A hardcore bot is for the group of a hardcore character: ask in the game, as that character.");

    Bots::BotLevelChoice normal = Bots::HardcoreBotLevel(5, {true, false, 30});
    EXPECT_FALSE(normal.ok);
    EXPECT_EQ(normal.error, "Only a hardcore character that is alive can make a hardcore bot.");
}
