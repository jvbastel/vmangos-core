/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "BotAI.h"
#include "BotMgr.h"
#include "Player.h"
#include "MotionMaster.h"
#include "Utilities/Random.h"
#include "Map.h"
#include "Log.h"
#include "Opcodes.h"

namespace
{
    // A bot out of combat thinks more often within this distance of a player.
    float const NearPlayerYards = 100.0f;
    // A bot follows or comes to a player within this distance on its map.
    float const CommandReachYards = 100.0f;
    // A following bot keeps this distance to the player, at a random angle.
    float const MinFollowYards = 3.0f;
    float const MaxFollowYards = 6.0f;
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

BotAI* BotAI::Of(Player const* player)
{
    if (!player || !player->IsBot())
        return nullptr;
    PlayerBotEntry* entry = player->GetSession()->GetBot();
    if (!entry)
        return nullptr;
    return dynamic_cast<BotAI*>(entry->ai.get());
}

void BotAI::PushCommand(BotCommandMessage const& message)
{
    m_commands.Push(message);
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
    for (BotCommandMessage const& message : m_commands.Drain())
        HandleCommand(message);

    Bots::ThinkState state = Bots::ThinkStateFor(me->IsInCombat(), IsNearPlayer());
    if (m_thinkTimer.Advance(diff, state))
        Think();
}

void BotAI::HandlePacket(WorldPacket const& packet)
{
    // The bot reacts to these packets in later tickets.
    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[Bots] %s got %s", me->GetName(), LookupOpcodeName(packet.GetOpcode()));
}

void BotAI::HandleCommand(BotCommandMessage const& message)
{
    Player* player = me->GetMap()->GetPlayer(message.player);
    Bots::CommandFacts const facts = {me->IsAlive(), IsInReach(player)};
    Bots::CommandOutcome const outcome = Bots::ApplyBotCommand(m_commandState, message.kind, facts);
    if (!outcome.obeys)
    {
        // The bot manager sends the line on the world thread.
        sBotMgr.QueueLine({me->GetObjectGuid(), message.chatType, message.player, outcome.line});
        return;
    }

    m_commandState = outcome.state;
    m_commandPlayer = message.player;
    if (message.kind == Bots::BotCommandKind::Stop)
    {
        me->AttackStop();
        me->InterruptNonMeleeSpells(false);
    }
    StartCommand(player);
}

void BotAI::StartCommand(Player* player)
{
    MotionMaster* motion = me->GetMotionMaster();
    motion->Clear();
    switch (m_commandState)
    {
        case Bots::CommandState::Follow:
            motion->MoveFollow(player, frand(MinFollowYards, MaxFollowYards), frand(0.0f, 2 * M_PI_F));
            return;
        case Bots::CommandState::Come:
            motion->MovePoint(0, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), MOVE_PATHFINDING | MOVE_RUN_MODE);
            return;
        case Bots::CommandState::Stay:
        case Bots::CommandState::None:
            break;
    }
    me->StopMoving();
    motion->MoveIdle();
}

void BotAI::KeepCommand()
{
    // A following bot follows again after a teleport or a death, when the
    // player is in reach.
    if (m_commandState != Bots::CommandState::Follow || !me->IsAlive())
        return;
    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
        return;
    Player* player = CommandPlayer();
    if (IsInReach(player))
        StartCommand(player);
}

Player* BotAI::CommandPlayer() const
{
    return m_commandPlayer ? me->GetMap()->GetPlayer(m_commandPlayer) : nullptr;
}

bool BotAI::IsInReach(Player const* player) const
{
    return player && player != me && player->IsInWorld() && player->IsWithinDist(me, CommandReachYards);
}

void BotAI::Think()
{
    // Version 1 starts with a bot that stands in the world.
    if (me->IsAlive() && me->GetStandState() != UNIT_STAND_STATE_STAND)
        me->SetStandState(UNIT_STAND_STATE_STAND);
    KeepCommand();
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
