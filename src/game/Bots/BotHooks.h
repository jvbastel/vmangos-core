/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_BOTHOOKS_H
#define WOWAI_GAME_BOTHOOKS_H

#include "Common.h"

#include <string>

class Player;

// The calls of the WOWAI bots in upstream core files. Each hook is one line
// there; the work happens here.
namespace BotHooks
{
    // WorldSession::HandleChatMessageOpcode, in the party and whisper
    // branches, after the core sent the message normally. `receiver` is the
    // whispered player, or null for party chat. A bot command goes into the
    // inboxes of the addressed bots.
    //
    // Threads: the core handles party chat and whispers on the async packet
    // thread, at the same time as the maps. So this only reads the group and
    // pushes into the locked inboxes of the bots.
    void OnPlayerChat(Player* sender, uint32 type, Player* receiver, std::string const& text);
}

#endif
