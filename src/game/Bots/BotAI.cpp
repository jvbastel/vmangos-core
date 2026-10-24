/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "BotAI.h"
#include "Player.h"
#include "Map.h"
#include "Log.h"
#include "Opcodes.h"

namespace
{
    // A bot out of combat thinks more often within this distance of a player.
    float const NearPlayerYards = 100.0f;
}

BotAI::BotAI(Bots::ThinkSettings const& thinkSettings) : PlayerBotAI(nullptr), m_thinkTimer(thinkSettings)
{
}

bool BotAI::IsInboxOpcode(uint16 opcode)
{
    switch (opcode)
    {
        case SMSG_PARTYKILLLOG:
        case SMSG_LOOT_START_ROLL:
        case SMSG_LOOT_ROLL_WON:
        case SMSG_RESURRECT_REQUEST:
        case SMSG_GROUP_INVITE:
        case MSG_MOVE_TELEPORT_ACK:
        case SMSG_NEW_WORLD:
        case SMSG_TRANSFER_PENDING:
            return true;
        default:
            return false;
    }
}

void BotAI::OnPacketReceived(WorldPacket const* packet)
{
    // The opcode first: most packets are of no use to the bot.
    if (!IsInboxOpcode(packet->GetOpcode()))
        return;
    m_inbox.Push(*packet);
}

void BotAI::UpdateAI(uint32 const diff)
{
    PlayerBotAI::UpdateAI(diff);
    if (!me || !me->IsInWorld())
        return;

    for (WorldPacket const& packet : m_inbox.Drain())
        HandlePacket(packet);

    Bots::ThinkState state = Bots::ThinkStateFor(me->IsInCombat(), IsNearPlayer());
    if (m_thinkTimer.Advance(diff, state))
        Think();
}

void BotAI::HandlePacket(WorldPacket const& packet)
{
    // The bot reacts to these packets in later tickets.
    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[Bots] %s got %s", me->GetName(), LookupOpcodeName(packet.GetOpcode()));
}

void BotAI::Think()
{
    // Version 1 starts with a bot that stands in the world.
    if (me->IsAlive() && me->GetStandState() != UNIT_STAND_STATE_STAND)
        me->SetStandState(UNIT_STAND_STATE_STAND);
}

bool BotAI::IsNearPlayer() const
{
    for (auto const& ref : me->GetMap()->GetPlayers())
    {
        Player const* player = ref.getSource();
        if (player && player != me && !player->IsBot() && player->IsWithinDist(me, NearPlayerYards))
            return true;
    }
    return false;
}
