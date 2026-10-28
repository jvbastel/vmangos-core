#ifndef WOWAI_BOTS_BOT_PANEL_H
#define WOWAI_BOTS_BOT_PANEL_H

#include "BotRecipe.h"
#include "GearTemplate.h"

#include <string>
#include <vector>

// jvbastel/WOWAI#198: the bot panel of the role choice add-on
// (wowai/addons/WowaiRole/WowaiBots.lua). The add-on asks for the list with a
// party add-on message. The server answers with party add-on messages to the
// player alone: the group bots of the player's faction, by role. The Invite
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
        GearFaction faction;
        bool worldBot; // Not for groups: never on the panel.
    };

    // The group bots of the faction, in the role order tank, healer, melee,
    // ranged, and by name within a role.
    std::vector<PanelBot> PanelBotsFor(std::vector<PanelBot> bots, GearFaction faction);

    // A party add-on message as the server gets it ("<prefix>\t<text>"):
    // true for the list request "WOWAI_BOTS\t1:list" of the add-on.
    bool IsBotPanelRequest(std::string const& message);

    // The answer to a list request, each a full add-on message: "1:clear",
    // then "1:bot:<role word>:<name>" for each bot in order, then "1:end".
    // One message per bot keeps each one far below the 255 bytes of a chat
    // message.
    std::vector<std::string> BotPanelMessages(std::vector<PanelBot> const& listed);
}

#endif
