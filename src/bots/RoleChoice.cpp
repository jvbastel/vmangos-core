#include "RoleChoice.h"

namespace Bots
{
    namespace
    {
        struct RoleChoiceWord
        {
            char const* word;
            RoleChoice choice;
        };

        RoleChoiceWord const RoleChoiceWords[] = {
            {"tank", RoleChoice::Tank},
            {"healer", RoleChoice::Healer},
            {"melee", RoleChoice::Melee},
            {"ranged", RoleChoice::Ranged},
            {"none", RoleChoice::None},
        };
    }

    RoleChoiceMessage ParseRoleChoiceMessage(std::string const& message)
    {
        RoleChoiceMessage parsed = {false, RoleChoice::None};
        std::string const prefix = std::string(RoleChoicePrefix) + "\t";
        if (message.compare(0, prefix.size(), prefix) != 0)
            return parsed;
        parsed.ours = true;

        std::string const versioned = std::string(RoleChoiceVersion) + ":";
        std::string const text = message.substr(prefix.size());
        if (text.compare(0, versioned.size(), versioned) != 0)
            return parsed;
        std::string const word = text.substr(versioned.size());
        for (RoleChoiceWord const& known : RoleChoiceWords)
            if (word == known.word)
                parsed.choice = known.choice;
        return parsed;
    }
}
