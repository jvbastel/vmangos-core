#ifndef WOWAI_BOTS_BOT_RECIPE_H
#define WOWAI_BOTS_BOT_RECIPE_H

#include <cstdint>
#include <string>
#include <vector>

// What the GM command that creates a bot asks for: a fixed name, a class, a
// role, and optionally a race or a faction. The bots are all level 16. The game layer maps these values to its own races and
// classes; the library has no game types.
namespace Bots
{
    std::uint32_t const Version1BotLevel = 16;

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
        std::uint32_t level;
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
    // [gear <template>]`, in any case. With no race, the class gets its
    // default race of the faction (Horde when no word names one). Horde: an
    // Orc Warrior, a Troll Priest, an Orc Rogue, an Undead Warlock, an Undead
    // Mage. Alliance: a Human Warrior, a Dwarf Priest, a Human Rogue, a Gnome
    // Warlock, a Gnome Mage. The name is not checked here: the game layer
    // checks it like a player name.
    BotRecipeResult ParseBotRecipe(std::vector<std::string> const& words);

    // The word of the role in the command and in the `bot_character` table.
    char const* RoleName(BotRole role);
    // The word of the class in the command and in data files.
    char const* ClassName(BotClass botClass);
    // A class or role word of the command, in any case. False if unknown.
    bool ParseClassName(std::string const& word, BotClass& botClass);
    bool ParseRoleName(std::string const& word, BotRole& role);
    // True when the class can play the role in a group.
    bool ClassCanPlayRole(BotClass botClass, BotRole role);
}

#endif
