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
// in an upstream file and does nothing for a player who is not our bot.
namespace BotHooks
{
    // At the end of Player::GiveLevel: runs the level routine for our bots.
    void OnLevelUp(Player* player);

    // WorldSession::HandleChatMessageOpcode, in the party and whisper
    // branches, after the core sent the message normally. `receiver` is the
    // whispered player, or null for party chat. A bot command goes into the
    // inboxes of the addressed bots.
    //
    // Threads: the core handles party chat and whispers on the async packet
    // thread, at the same time as the maps. So this only reads the group and
    // pushes into the locked inboxes of the bots.
    void OnPlayerChat(Player* sender, uint32 type, Player* receiver, std::string const& text);

    // WorldSession::HandleChatMessageOpcode, in the whisper branch, when no
    // player of that name is online. A whisper of `invite` or `dismiss` to an
    // offline bot goes to the bot manager, so that a player with no group
    // can invite the first bot. True: the hook took the whisper, and the core
    // sends no "player not found" notice. Same threads as OnPlayerChat.
    bool OnWhisperToOffline(Player* sender, std::string const& receiverName, std::string const& text);

    // jvbastel/WOWAI#198: WorldSession::HandleChatMessageOpcode, at the top
    // of the whisper branch (not for LANG_ADDON). A whisper of the WowaiRole
    // add-on to the sender's own name (bots/AddonWhisper.h): the list
    // request of the bot panel queues the panel for the sender
    // (BotMgr::QueueBotPanelRequest), and a role choice (spec #188 fix-up S4)
    // sets the role choice of the sender (BotMgr::SetRoleChoice). True: the
    // hook took the whisper, and the core does not send it. Same threads as
    // OnPlayerChat.
    bool OnWhisperToSelf(Player* sender, std::string const& receiverName, std::string const& text);
}

#endif
