/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_BOTAI_H
#define WOWAI_GAME_BOTAI_H

#include "PlayerBotAI.h"
#include "ObjectGuid.h"
#include "WorldPacket.h"
#include "BotCommand.h"
#include "CommandState.h"
#include "Inbox.h"
#include "ThinkTimer.h"

// A bot command for one bot, from the chat of a player.
struct BotCommandMessage
{
    Bots::BotCommandKind kind;
    ObjectGuid player; // The player who gave the command.
    uint32 chatType;   // CHAT_MSG_PARTY or CHAT_MSG_WHISPER: where a reply goes.
};

// The AI of one WOWAI bot. It derives from PlayerBotAI, which acks teleports
// and receives the packets of the socketless session. It is no PartyBotAI.
//
// Threads: the packet senders call OnPacketReceived from any thread. It only
// copies the packets that the bots use into the locked inbox. UpdateAI runs
// on the map thread: it empties the inbox, and then the bot thinks when its
// think timer is due. All decisions happen there.
class BotAI : public PlayerBotAI
{
    public:
        explicit BotAI(Bots::ThinkSettings const& thinkSettings);

        // True for the packets that the bots use: kill log, loot roll start
        // and win, resurrect request, group invite, and teleport.
        static bool IsInboxOpcode(uint16 opcode);

        // The AI of a WOWAI bot, or null for any other player.
        static BotAI* Of(Player const* player);

        // From any thread: the bot handles the command in its next update.
        void PushCommand(BotCommandMessage const& message);

        void OnPacketReceived(WorldPacket const* packet) override;
        void UpdateAI(uint32 const diff) override;

    private:
        void HandlePacket(WorldPacket const& packet);
        void HandleCommand(BotCommandMessage const& message);
        void StartCommand(Player* player);
        void KeepCommand();
        Player* CommandPlayer() const;
        bool IsInReach(Player const* player) const;
        void Think();
        bool IsNearPlayer() const;

        Bots::Inbox<WorldPacket> m_inbox;
        Bots::Inbox<BotCommandMessage> m_commands;
        Bots::CommandState m_commandState = Bots::CommandState::None;
        ObjectGuid m_commandPlayer; // The player of the last command that the bot obeyed.
        Bots::ThinkTimer m_thinkTimer;
};

#endif
