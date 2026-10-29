#include "RoleChoice.h"
#include "AddonWhisper.h"

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

    RoleChoiceMessage ParseRoleChoiceWhisper(std::string const& sender, std::string const& receiver,
        std::string const& text)
    {
        RoleChoiceMessage parsed = {false, RoleChoice::None};
        std::string const prefix = std::string(RoleChoicePrefix) + " ";
        if (!IsWhisperToSelf(sender, receiver) || text.compare(0, prefix.size(), prefix) != 0)
            return parsed;
        parsed.ours = true;
        for (RoleChoiceWord const& known : RoleChoiceWords)
            if (text == AddonText(RoleChoicePrefix, RoleChoiceVersion, known.word))
                parsed.choice = known.choice;
        return parsed;
    }
}
