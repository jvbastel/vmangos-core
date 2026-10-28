#include "Faction.h"

namespace Bots
{
    bool ParseFaction(std::string const& word, Faction& faction)
    {
        if (word == "horde")
            faction = Faction::Horde;
        else if (word == "alliance")
            faction = Faction::Alliance;
        else
            return false;
        return true;
    }
}
