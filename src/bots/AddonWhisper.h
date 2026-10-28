#ifndef WOWAI_BOTS_ADDON_WHISPER_H
#define WOWAI_BOTS_ADDON_WHISPER_H

#include <string>

// Spec #188 fix-up (S4): the WowaiRole add-on (wowai/addons/WowaiRole) talks
// to the server with whispers to the player's own name, which the 1.12 client
// sends also with no group: the bot panel's list request (BotPanel.h) and the
// role choice (RoleChoice.h). The server takes such a whisper
// (BotHooks::OnWhisperToSelf): no whisper line goes out. The same frame
// carries the server's system messages to the panel.
namespace Bots
{
    // `receiver` is the name of `sender`, in any case.
    bool IsWhisperToSelf(std::string const& sender, std::string const& receiver);

    // The frame of an add-on text: "<prefix> <version>:<text>".
    std::string AddonText(char const* prefix, char const* version, std::string const& text);
}

#endif
