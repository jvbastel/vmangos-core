#include "BotRecipe.h"
#include "Faction.h"

#include <cctype>

namespace Bots
{
    namespace
    {
        struct ClassInfo
        {
            BotClass botClass;
            char const* name;
            // The races that can have the class. The first race of each
            // faction is the default race of the class in that faction.
            std::vector<BotRace> races;
            std::vector<BotRole> roles;
        };

        struct RaceInfo
        {
            BotRace race;
            char const* name;
            Faction faction;
        };

        // The race and class pairs of the game, and the roles that each
        // class can play in a group.
        std::vector<ClassInfo> const& Classes()
        {
            static std::vector<ClassInfo> const classes = {
                {BotClass::Warrior, "warrior",
                    {BotRace::Orc, BotRace::Undead, BotRace::Tauren, BotRace::Troll,
                        BotRace::Human, BotRace::Dwarf, BotRace::NightElf, BotRace::Gnome},
                    {BotRole::Tank, BotRole::Melee}},
                {BotClass::Priest, "priest",
                    {BotRace::Troll, BotRace::Undead, BotRace::Dwarf, BotRace::Human, BotRace::NightElf},
                    {BotRole::Healer, BotRole::Ranged}},
                {BotClass::Rogue, "rogue",
                    {BotRace::Orc, BotRace::Undead, BotRace::Troll,
                        BotRace::Human, BotRace::Dwarf, BotRace::NightElf, BotRace::Gnome},
                    {BotRole::Melee}},
                {BotClass::Warlock, "warlock",
                    {BotRace::Undead, BotRace::Orc, BotRace::Gnome, BotRace::Human}, {BotRole::Ranged}},
                {BotClass::Mage, "mage",
                    {BotRace::Undead, BotRace::Troll, BotRace::Gnome, BotRace::Human}, {BotRole::Ranged}},
                {BotClass::Hunter, "hunter",
                    {BotRace::Orc, BotRace::Tauren, BotRace::Troll, BotRace::Dwarf, BotRace::NightElf},
                    {BotRole::Ranged}},
                {BotClass::Shaman, "shaman",
                    {BotRace::Orc, BotRace::Tauren, BotRace::Troll},
                    {BotRole::Healer, BotRole::Melee, BotRole::Ranged}},
                {BotClass::Druid, "druid",
                    {BotRace::Tauren, BotRace::NightElf},
                    {BotRole::Tank, BotRole::Healer, BotRole::Melee, BotRole::Ranged}},
            };
            return classes;
        }

        std::vector<RaceInfo> const& Races()
        {
            static std::vector<RaceInfo> const races = {
                {BotRace::Orc, "orc", Faction::Horde},
                {BotRace::Undead, "undead", Faction::Horde},
                {BotRace::Tauren, "tauren", Faction::Horde},
                {BotRace::Troll, "troll", Faction::Horde},
                {BotRace::Human, "human", Faction::Alliance},
                {BotRace::Dwarf, "dwarf", Faction::Alliance},
                {BotRace::NightElf, "nightelf", Faction::Alliance},
                {BotRace::Gnome, "gnome", Faction::Alliance},
            };
            return races;
        }

        RaceInfo const& InfoOf(BotRace race)
        {
            for (RaceInfo const& info : Races())
                if (info.race == race)
                    return info;
            return Races().front();
        }

        // The default race of the class in the faction. False when no race
        // of the faction can have the class.
        bool DefaultRace(ClassInfo const& classInfo, Faction faction, BotRace& race)
        {
            for (BotRace r : classInfo.races)
                if (InfoOf(r).faction == faction)
                {
                    race = r;
                    return true;
                }
            return false;
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
            return InfoOf(race).name;
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

    char const* ClassName(BotClass botClass)
    {
        for (ClassInfo const& info : Classes())
            if (info.botClass == botClass)
                return info.name;
        return "";
    }

    bool ParseClassName(std::string const& word, BotClass& botClass)
    {
        std::string const lower = Lower(word);
        for (ClassInfo const& info : Classes())
            if (lower == info.name)
            {
                botClass = info.botClass;
                return true;
            }
        return false;
    }

    bool ParseRoleName(std::string const& word, BotRole& role)
    {
        std::string const lower = Lower(word);
        for (BotRole r : AllRoles)
            if (lower == RoleName(r))
            {
                role = r;
                return true;
            }
        return false;
    }

    bool ClassCanPlayRole(BotClass botClass, BotRole role)
    {
        for (ClassInfo const& info : Classes())
            if (info.botClass == botClass)
                return Contains(info.roles, role);
        return false;
    }

    BotRecipeResult ParseBotRecipe(std::vector<std::string> const& allWords)
    {
        char const* const usage = "Usage: <name> <class> <role> [race|horde|alliance] [gear <template>]";
        std::vector<std::string> words = allWords;
        std::string gearTemplate;
        for (std::size_t i = 3; i < words.size(); ++i)
            if (Lower(words[i]) == "gear")
            {
                if (i + 2 != words.size())
                    return Fail(usage);
                gearTemplate = Lower(words[i + 1]);
                words.resize(i);
                break;
            }
        if (words.size() < 3 || words.size() > 4)
            return Fail(usage);

        std::string const classWord = Lower(words[1]);
        ClassInfo const* classInfo = nullptr;
        for (ClassInfo const& info : Classes())
            if (classWord == info.name)
                classInfo = &info;
        if (!classInfo)
            return Fail("Unknown class '" + words[1] +
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

        // The fourth word is a race, or a faction for the default race of
        // the class in it.
        Faction faction = Faction::Horde;
        BotRace race = BotRace::Orc;
        bool raceFound = false;
        if (words.size() == 4)
        {
            std::string const word = Lower(words[3]);
            for (RaceInfo const& info : Races())
                if (word == info.name)
                {
                    race = info.race;
                    raceFound = true;
                }
            if (raceFound && !Contains(classInfo->races, race))
                return Fail(WithArticle(RaceName(race)) + " cannot be a " + classInfo->name + ".");
            if (!raceFound && !ParseFaction(word, faction))
                return Fail("Unknown race '" + words[3] +
                    "'. Use orc, undead, tauren, troll, human, dwarf, nightelf, gnome, horde, or alliance.");
        }
        if (!raceFound && !DefaultRace(*classInfo, faction, race))
            return Fail(std::string("The ") + Lower(words[3]) + " has no " + classInfo->name + ".");

        BotRecipeResult result;
        result.ok = true;
        result.recipe.name = words[0];
        result.recipe.race = race;
        result.recipe.botClass = classInfo->botClass;
        result.recipe.role = role;
        result.recipe.level = Version1BotLevel;
        result.recipe.gearTemplate = gearTemplate;
        return result;
    }
}
