#ifndef WOWAI_BOTS_BOT_RECIPE_H
#define WOWAI_BOTS_BOT_RECIPE_H

#include <cstdint>
#include <string>
#include <vector>

// What the GM command that creates a bot asks for: a fixed name, a class, a
// role, and optionally a race or a faction. A normal bot is level 16; a
// hardcore bot is at or below the level of the player who asks. The game
// layer maps these values to its own races and classes; the library has no
// game types.
namespace Bots
{
    std::uint32_t const Version1BotLevel = 16;
    // jvbastel/WOWAI#257: the level of a hardcore bot with no level word: the
    // level of the player who asks (HardcoreBotLevel).
    std::uint32_t const PlayerLevel = 0;
    std::uint32_t const MaxBotLevel = 60;

    enum class BotRace
    {
        Orc,
        Undead,
        Tauren,
        Troll,
        Human,
        Dwarf,
        NightElf,
        Gnome,
    };

    enum class BotClass
    {
        Warrior,
        Priest,
        Rogue,
        Warlock,
        Mage,
        Hunter,
        Shaman,
        Druid,
    };

    enum class BotRole
    {
        Tank,
        Healer,
        Melee,  // Melee damage.
        Ranged, // Ranged damage.
    };

    struct BotRecipe
    {
        std::string name;
        BotRace race;
        BotClass botClass;
        BotRole role;
        // Version1BotLevel for a normal bot. A hardcore bot: the level of
        // the `hardcore` word, or PlayerLevel (see HardcoreBotLevel).
        std::uint32_t level;
        // jvbastel/WOWAI#257: a hardcore bot, for the group of a hardcore
        // character. Its death is final, as the player's.
        bool hardcore = false;
        // The name of a gear template in lower case, or empty for the
        // generated template of the class and role.
        std::string gearTemplate;
    };

    struct BotRecipeResult
    {
        bool ok;
        BotRecipe recipe;  // Only when ok.
        std::string error; // A short line for the GM, when not ok.
    };

    // Reads the words `<name> <class> <role> [race|horde|alliance]
    // [gear <template>] [hardcore [level]]`, in any case. The level of a
    // hardcore bot is a number from 1 to MaxBotLevel. With no race, the class gets its
    // default race of the faction (Horde when no word names one). Horde: an
    // Orc Warrior, a Troll Priest, an Orc Rogue, an Undead Warlock, an Undead
    // Mage. Alliance: a Human Warrior, a Dwarf Priest, a Human Rogue, a Gnome
    // Warlock, a Gnome Mage. The name is not checked here: the game layer
    // checks it like a player name.
    BotRecipeResult ParseBotRecipe(std::vector<std::string> const& words);

    // jvbastel/WOWAI#257: the character that asks for a hardcore bot.
    struct HardcoreAsker
    {
        bool inGame;          // A character in the game asks (not the console or SOAP).
        bool hardcoreAlive;   // That character is a hardcore character that is alive.
        std::uint32_t level;  // Its level.
    };

    struct BotLevelChoice
    {
        bool ok;
        std::uint32_t level; // Only when ok.
        std::string error;   // A short line for the asker, when not ok.
    };

    // The level of a new hardcore bot: `wanted` (PlayerLevel: the asker's
    // level), at or below the asker's level, so that a dead bot can be
    // replaced at the player's level. Only a hardcore character that is
    // alive and in the game gets one.
    BotLevelChoice HardcoreBotLevel(std::uint32_t wanted, HardcoreAsker const& asker);

    // The word of the role in the command and in the `bot_character` table.
    char const* RoleName(BotRole role);
    // The word of the class in the command and in data files.
    char const* ClassName(BotClass botClass);
    // A class or role word of the command, in any case. False if unknown.
    bool ParseClassName(std::string const& word, BotClass& botClass);
    bool ParseRoleName(std::string const& word, BotRole& role);
    // True when the class can play the role in a group.
    bool ClassCanPlayRole(BotClass botClass, BotRole role);

    // jvbastel/WOWAI#209: whether the bot manager may make or log in a bot of the class. With the
    // Forever rules on (`foreverRules`), the player plays the Warrior, so a Warrior bot is refused.
    // Empty when allowed, else a short line for the GM and the log.
    std::string BotClassRefusal(BotClass botClass, bool foreverRules);
}

#endif
