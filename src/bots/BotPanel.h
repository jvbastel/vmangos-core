#ifndef WOWAI_BOTS_BOT_PANEL_H
#define WOWAI_BOTS_BOT_PANEL_H

#include "BotRecipe.h"
#include "Faction.h"

#include <string>
#include <vector>

// jvbastel/WOWAI#198: the bot panel of the role choice add-on
// (wowai/addons/WowaiRole/WowaiBots.lua). The add-on asks for the list with a
// whisper to the player's own name, and the server answers with system
// messages to the player alone: the group bots of the player's faction, by
// role. Both work with no group (the 1.12 client sends a party add-on message
// only in a party, and the panel is mostly used before the first bot joins).
// The server takes the whisper, so the player sees no whisper line, and the
// add-on hides the system messages from the chat frames. The Invite
// and Dismiss buttons whisper `invite` and `dismiss` to the bot, the same
// path as the typed bot commands (GroupCommand.h).
namespace Bots
{
    // The add-on prefix of the panel messages, and the version of the text.
    char const* const BotPanelPrefix = "WOWAI_BOTS";
    char const* const BotPanelVersion = "1";

    // A bot of the `bot_character` table, as the panel needs it.
    struct PanelBot
    {
        std::string name;
        BotRole role;
        Faction faction;
        bool worldBot; // Not for groups: never on the panel.
    };

    // The group bots of the faction, in the role order tank, healer, melee,
    // ranged, and by name within a role.
    std::vector<PanelBot> PanelBotsFor(std::vector<PanelBot> bots, Faction faction);

    // A whisper as the server gets it: true for the list request
    // "WOWAI_BOTS 1:list" of the add-on, whispered by `sender` to its own
    // name (`receiver`, in any case).
    bool IsBotPanelRequest(std::string const& sender, std::string const& receiver, std::string const& text);

    // The answer to a list request, each a full system message:
    // "WOWAI_BOTS 1:clear", then "WOWAI_BOTS 1:bot:<role word>:<name>" for
    // each bot in order, then "WOWAI_BOTS 1:end". One message per bot keeps
    // each one far below the 255 bytes of a chat message.
    std::vector<std::string> BotPanelMessages(std::vector<PanelBot> const& listed);
}

#endif
