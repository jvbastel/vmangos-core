#ifndef WOWAI_BOTS_FACTION_H
#define WOWAI_BOTS_FACTION_H

#include <string>

// The faction of a character: of a bot recipe (BotRecipe.h), of a gear
// template line (GearTemplate.h), of a bot on the bot panel (BotPanel.h).
namespace Bots
{
    enum class Faction
    {
        Horde,
        Alliance,
    };

    // The faction word `word`, "horde" or "alliance" in lower case, as the
    // recipes and the gear templates write it. False for another word, and
    // `faction` stays.
    bool ParseFaction(std::string const& word, Faction& faction);
}

#endif
