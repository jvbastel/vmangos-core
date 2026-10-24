/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_BOTAI_H
#define WOWAI_GAME_BOTAI_H

#include "PlayerBotAI.h"
#include "WorldPacket.h"
#include "Inbox.h"
#include "ThinkTimer.h"

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

        void OnPacketReceived(WorldPacket const* packet) override;
        void UpdateAI(uint32 const diff) override;

    private:
        void HandlePacket(WorldPacket const& packet);
        void Think();
        bool IsNearPlayer() const;

        Bots::Inbox<WorldPacket> m_inbox;
        Bots::ThinkTimer m_thinkTimer;
};

#endif
