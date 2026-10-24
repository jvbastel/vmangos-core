#include "BotRecipe.h"

#include <cctype>

namespace Bots
{
    namespace
    {
        struct ClassInfo
        {
            BotClass botClass;
            char const* name;
            BotRace defaultRace;
            std::vector<BotRace> races;
            std::vector<BotRole> roles;
        };

        struct RaceInfo
        {
            BotRace race;
            char const* name;
        };

        // The Horde race and class pairs of the game, and the roles that each
        // class can play in a group.
        std::vector<ClassInfo> const& Classes()
        {
            static std::vector<ClassInfo> const classes = {
                {BotClass::Warrior, "warrior", BotRace::Orc,
                    {BotRace::Orc, BotRace::Undead, BotRace::Tauren, BotRace::Troll}, {BotRole::Tank, BotRole::Melee}},
                {BotClass::Priest, "priest", BotRace::Troll,
                    {BotRace::Undead, BotRace::Troll}, {BotRole::Healer, BotRole::Ranged}},
                {BotClass::Rogue, "rogue", BotRace::Orc,
                    {BotRace::Orc, BotRace::Undead, BotRace::Troll}, {BotRole::Melee}},
                {BotClass::Warlock, "warlock", BotRace::Undead,
                    {BotRace::Orc, BotRace::Undead}, {BotRole::Ranged}},
                {BotClass::Mage, "mage", BotRace::Undead,
                    {BotRace::Undead, BotRace::Troll}, {BotRole::Ranged}},
                {BotClass::Hunter, "hunter", BotRace::Orc,
                    {BotRace::Orc, BotRace::Tauren, BotRace::Troll}, {BotRole::Ranged}},
                {BotClass::Shaman, "shaman", BotRace::Orc,
                    {BotRace::Orc, BotRace::Tauren, BotRace::Troll}, {BotRole::Healer, BotRole::Melee, BotRole::Ranged}},
                {BotClass::Druid, "druid", BotRace::Tauren,
                    {BotRace::Tauren}, {BotRole::Tank, BotRole::Healer, BotRole::Melee, BotRole::Ranged}},
            };
            return classes;
        }

        std::vector<RaceInfo> const& Races()
        {
            static std::vector<RaceInfo> const races = {
                {BotRace::Orc, "orc"},
                {BotRace::Undead, "undead"},
                {BotRace::Tauren, "tauren"},
                {BotRace::Troll, "troll"},
            };
            return races;
        }

        BotRole const AllRoles[] = {BotRole::Tank, BotRole::Healer, BotRole::Melee, BotRole::Ranged};

        std::string Lower(std::string text)
        {
            for (char& c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        // "An orc", "A troll".
        std::string WithArticle(char const* word)
        {
            bool vowel = std::string("aeiou").find(word[0]) != std::string::npos;
            return std::string(vowel ? "An " : "A ") + word;
        }

        char const* RaceName(BotRace race)
        {
            for (RaceInfo const& info : Races())
                if (info.race == race)
                    return info.name;
            return "";
        }

        template <typename T>
        bool Contains(std::vector<T> const& values, T value)
        {
            for (T const& v : values)
                if (v == value)
                    return true;
            return false;
        }

        BotRecipeResult Fail(std::string error)
        {
            BotRecipeResult result;
            result.ok = false;
            result.error = error;
            return result;
        }
    }

    char const* RoleName(BotRole role)
    {
        switch (role)
        {
            case BotRole::Tank:
                return "tank";
            case BotRole::Healer:
                return "healer";
            case BotRole::Melee:
                return "melee";
            case BotRole::Ranged:
                break;
        }
        return "ranged";
    }

    BotRecipeResult ParseBotRecipe(std::vector<std::string> const& words)
    {
        if (words.size() < 3 || words.size() > 4)
            return Fail("Usage: <name> <class> <role> [race]");

        std::string const classWord = Lower(words[1]);
        ClassInfo const* classInfo = nullptr;
        for (ClassInfo const& info : Classes())
            if (classWord == info.name)
                classInfo = &info;
        if (!classInfo)
            return Fail("Unknown Horde class '" + words[1] +
                "'. Use warrior, priest, rogue, warlock, mage, hunter, shaman, or druid.");

        std::string const roleWord = Lower(words[2]);
        BotRole role = BotRole::Tank;
        bool roleFound = false;
        for (BotRole r : AllRoles)
            if (roleWord == RoleName(r))
            {
                role = r;
                roleFound = true;
            }
        if (!roleFound)
            return Fail("Unknown role '" + words[2] + "'. Use tank, healer, melee, or ranged.");
        if (!Contains(classInfo->roles, role))
            return Fail(WithArticle(classInfo->name) + " cannot play the " + RoleName(role) + " role.");

        BotRace race = classInfo->defaultRace;
        if (words.size() == 4)
        {
            std::string const raceWord = Lower(words[3]);
            bool raceFound = false;
            for (RaceInfo const& info : Races())
                if (raceWord == info.name)
                {
                    race = info.race;
                    raceFound = true;
                }
            if (!raceFound)
                return Fail("Unknown Horde race '" + words[3] + "'. Use orc, undead, tauren, or troll.");
            if (!Contains(classInfo->races, race))
                return Fail(WithArticle(RaceName(race)) + " cannot be a " + classInfo->name + ".");
        }

        BotRecipeResult result;
        result.ok = true;
        result.recipe.name = words[0];
        result.recipe.race = race;
        result.recipe.botClass = classInfo->botClass;
        result.recipe.role = role;
        result.recipe.level = Version1BotLevel;
        return result;
    }
}
