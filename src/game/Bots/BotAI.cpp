/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "BotAI.h"
#include "BotMgr.h"
#include "BotAdapter.h"
#include "ClassModules.h"
#include "DeathAndRest.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Player.h"
#include "MotionMaster.h"
#include "Utilities/Random.h"
#include "Map.h"
#include "Log.h"
#include "Opcodes.h"
#include "Packet.h"
#include "Packets/Misc.h"
#include "WorldSession.h"

#include <chrono>

namespace
{
    // A bot out of combat thinks more often within this distance of a player.
    float const NearPlayerYards = 100.0f;
    // A bot follows or comes to a player within this distance on its map.
    float const CommandReachYards = 100.0f;

    // The clock of the group events of the coordinator.
    int64 SteadyMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

BotAI::BotAI(Bots::ThinkSettings const& thinkSettings, Bots::BotRole role) :
    PlayerBotAI(nullptr), m_thinkTimer(thinkSettings), m_role(role)
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
    if (packet->GetOpcode() == SMSG_NEW_WORLD)
    {
        // A far teleport: the bot is out of the world, so UpdateAI does not
        // run until the ack. The session handles the ack at its next update,
        // like the ack of a client.
        if (me)
            me->GetSession()->QueuePacket(std::make_unique<NullClientPacket>(MSG_MOVE_WORLDPORT_ACK));
        return;
    }
    m_inbox.Push(*packet);
}

void BotAI::UpdateAI(uint32 const diff)
{
    PlayerBotAI::UpdateAI(diff);
    if (!me || !me->IsInWorld())
        return;

    // A new character starts with the intro cinematic. A client ends it with
    // CMSG_COMPLETE_CINEMATIC; until then, no creature can target the bot.
    if (me->GetCurrentCinematicEntry())
        me->CinematicEnd();

    for (WorldPacket const& packet : m_inbox.Drain())
        HandlePacket(packet);
    for (BotCommandMessage const& message : m_commands.Drain())
        HandleCommand(message);

    KeepLevelCorrect();

    Bots::ThinkState state = Bots::ThinkStateFor(me->IsInCombat(), IsNearPlayer());
    if (m_thinkTimer.Advance(diff, state))
        Think();
}

void BotAI::KeepLevelCorrect()
{
    // At the first update in the world after each login, so also after the
    // creation. The routine is idempotent, so a bot that is correct already
    // stays the same.
    if (!m_levelRoutineDone)
    {
        sBotMgr.ApplyLevelRoutine(me);
        m_levelRoutineDone = true;
    }

    // Each summon of a pet: the new pet gets its pet spells.
    Pet* pet = me->GetPet();
    ObjectGuid const petGuid = pet ? pet->GetObjectGuid() : ObjectGuid();
    if (petGuid != m_taughtPet)
    {
        if (pet)
            sBotMgr.GetLevelRoutine().TeachPetSpells(me, pet);
        m_taughtPet = petGuid;
    }
}

void BotAI::HandlePacket(WorldPacket const& packet)
{
    switch (packet.GetOpcode())
    {
        case SMSG_GROUP_INVITE:
            // A normal /invite: the bot accepts, like the client of a player.
            me->GetSession()->QueuePacket(std::make_unique<NullClientPacket>(CMSG_GROUP_ACCEPT));
            return;
        case SMSG_LOOT_START_ROLL:
            m_loot.OnStartRoll(me, packet);
            return;
        case SMSG_LOOT_ROLL_WON:
        {
            // Each bot of the group sees the roll end: the coordinator makes
            // one `loot_roll` event of it.
            BotLootRollWon const won = m_loot.OnRollWon(me, packet);
            if (Group* group = me->GetGroup())
                sBotMgr.GetCoordinator(group->GetId())->NoteRollWon(won.lootedTarget.GetRawValue(), won.itemSlot,
                    won.itemEntry, won.winner.GetRawValue(), SteadyMs());
            return;
        }
        case SMSG_PARTYKILLLOG:
            OnPartyKill(packet);
            return;
        case SMSG_RESURRECT_REQUEST:
            AcceptResurrect(packet);
            return;
        default:
            break;
    }
    // The bot reacts to the other packets in later tickets.
    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[Bots] %s got %s", me->GetName(), LookupOpcodeName(packet.GetOpcode()));
}

void BotAI::OnPartyKill(WorldPacket const& packet)
{
    // The group killed a creature. The member with the tap does not get the
    // packet, the others do. A boss of the dungeon's route file is a
    // `boss_kill`, once for the group.
    WorldPacket data(packet);
    ObjectGuid killer, victim;
    data >> killer >> victim;
    Group* group = me->GetGroup();
    if (!group || !victim.IsCreature() || !sBotMgr.IsBoss(me->GetMapId(), victim.GetEntry()))
        return;
    sBotMgr.GetCoordinator(group->GetId())->NoteBossKill(victim.GetRawValue(), victim.GetEntry(), SteadyMs());
}

void BotAI::HandleCommand(BotCommandMessage const& message)
{
    Player* player = me->GetMap()->GetPlayer(message.player);
    Bots::CommandFacts facts = {me->IsAlive(), IsInReach(player), m_role == Bots::BotRole::Tank, PullTargetOf(player),
        IsDamageBot()};
    facts.groupRests = GroupRests();
    Bots::CommandOutcome const outcome = Bots::ApplyBotCommand(m_commandState, message.kind, facts);
    if (!outcome.obeys)
    {
        // A bot that is no tank ignores `pull` without a line.
        if (!*outcome.line)
            return;
        // The bot manager sends the line on the world thread.
        sBotMgr.QueueLine({me->GetObjectGuid(), message.chatType, message.player, outcome.line});
        return;
    }

    if (message.kind == Bots::BotCommandKind::Pull)
    {
        StartPull(player);
        return;
    }
    if (message.kind == Bots::BotCommandKind::Attack)
    {
        // The damage bot switches at its next think (PickDamageTarget).
        m_attackTarget = player->GetSelectionGuid();
        sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] %s attacks %s", me->GetName(), m_attackTarget.GetString().c_str());
        return;
    }

    m_commandState = outcome.state;
    m_commandPlayer = message.player;
    if (message.kind == Bots::BotCommandKind::Stop)
    {
        m_attackTarget.Clear();
        me->AttackStop();
        me->InterruptNonMeleeSpells(false);
        std::shared_ptr<Bots::GroupCoordinator> coordinator = Coordinator();
        if (coordinator->GetPull().puller == me->GetObjectGuid().GetRawValue())
            coordinator->EndPull();
    }
    StartCommand(player);
}

Bots::PullTarget BotAI::PullTargetOf(Player const* player) const
{
    if (!player || !player->GetSelectionGuid())
        return Bots::PullTarget::None;
    Unit const* target = ObjectAccessor::GetUnit(*me, player->GetSelectionGuid());
    if (!target || !target->IsInWorld() || !target->IsInMap(me))
        return Bots::PullTarget::None;
    return me->IsValidAttackTarget(target) ? Bots::PullTarget::Enemy : Bots::PullTarget::NotEnemy;
}

bool BotAI::IsDamageBot() const
{
    return m_role == Bots::BotRole::Melee || m_role == Bots::BotRole::Ranged;
}

void BotAI::CheckAttackTarget()
{
    if (m_attackTarget.IsEmpty())
        return;
    Unit const* target = ObjectAccessor::GetUnit(*me, m_attackTarget);
    if (!target || !target->IsInWorld() || !target->IsInMap(me) || !target->IsAlive())
        m_attackTarget.Clear();
}

void BotAI::StartPull(Player* player)
{
    // The tank pulls at its next think: the pull rules of its strategy read
    // the pull state.
    ObjectGuid const target = player->GetSelectionGuid();
    Coordinator()->StartPull(me->GetObjectGuid().GetRawValue(), target.GetRawValue());
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] %s pulls %s", me->GetName(), target.GetString().c_str());
}

std::shared_ptr<Bots::GroupCoordinator> BotAI::Coordinator()
{
    if (Group* group = me->GetGroup())
        return sBotMgr.GetCoordinator(group->GetId());
    // No owner: the coordinator is a member of this AI.
    return std::shared_ptr<Bots::GroupCoordinator>(std::shared_ptr<Bots::GroupCoordinator>(), &m_soloCoordinator);
}

void BotAI::StartCommand(Player* player)
{
    MotionMaster* motion = me->GetMotionMaster();
    motion->Clear();
    switch (m_commandState)
    {
        case Bots::CommandState::Follow:
            BotAdapter(me).Follow(player->GetObjectGuid().GetRawValue());
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

bool BotAI::IsInReach(Player const* player) const
{
    return player && player != me && player->IsInWorld() && player->IsWithinDist(me, CommandReachYards);
}

void BotAI::AcceptResurrect(WorldPacket const& packet)
{
    // Like a click on Accept: the session resurrects the bot at its next
    // update, when the request is still valid.
    WorldPacket data(packet);
    ObjectGuid resurrector;
    data >> resurrector;
    auto response = std::make_unique<WorldPackets::Misc::ResurrectResponse>();
    response->resurrectorGuid = resurrector;
    response->accept = true;
    me->GetSession()->QueuePacket(std::move(response));
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] %s accepts the resurrect of %s", me->GetName(), resurrector.GetString().c_str());
}

bool BotAI::GroupRests()
{
    for (Bots::Guid bot : Coordinator()->GetResting())
    {
        // Only a bot that is still here: a bot that logged out while it ate
        // stays in the list of the coordinator.
        Player const* player = me->GetMap()->GetPlayer(ObjectGuid(bot));
        if (player && player->IsAlive() && player->GetGroup() == me->GetGroup())
            return true;
    }
    return false;
}

void BotAI::Think()
{
    // A fight or a death ends the corpse work.
    if (!me->IsAlive() || me->IsInCombat())
        m_loot.Stop();

    if (!m_strategiesMade)
    {
        Bots::BotClass botClass = Bots::BotClass::Warrior;
        if (BotClassOf(me->GetClass(), botClass))
            m_strategies = Bots::MakeStrategyStack(botClass, m_role);
        else
        {
            m_strategies.Push(Bots::MakeCommandStrategy());
            m_strategies.Push(Bots::MakeRestStrategy());
            m_strategies.Push(Bots::MakeDeathStrategy());
        }
        m_strategiesMade = true;
    }

    CheckAttackTarget();
    BotAdapter adapter(me, &m_loot);
    // Version 1 starts with a bot that stands in the world. A bot that eats
    // or drinks sits until it is full or a fight starts: standing up ends
    // the meal. A bot that is full again (a heal, a GM `.replenish`) stands
    // up at once, so the group does not wait for its food aura to run out.
    bool const rests = Bots::RestsNow(adapter);
    if (me->IsAlive() && me->GetStandState() != UNIT_STAND_STATE_STAND && (!rests || me->IsInCombat()))
        me->SetStandState(UNIT_STAND_STATE_STAND);

    std::shared_ptr<Bots::GroupCoordinator> coordinator = Coordinator();
    // For the chase limit of the damage bots (GroupRules.h).
    if (m_role == Bots::BotRole::Tank)
        coordinator->SetTank(me->GetObjectGuid().GetRawValue());
    std::string const reaction = m_bossReaction.Active();
    switch (m_bossReaction.Update(adapter, *coordinator, m_strategies, m_role))
    {
        case Bots::BossReactionSlot::Change::Pushed:
            sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] %s pushes the boss reaction %s (boss 0x%016llx)", me->GetName(),
                m_bossReaction.Active().c_str(), static_cast<unsigned long long>(coordinator->GetBossFight().boss));
            break;
        case Bots::BossReactionSlot::Change::Popped:
            sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] %s pops the boss reaction %s", me->GetName(), reaction.c_str());
            break;
        case Bots::BossReactionSlot::Change::None:
            break;
    }
    Bots::WatchGroup(adapter, *coordinator);
    Bots::BotContext const context = {adapter, adapter, *coordinator, m_commandState, m_commandPlayer.GetRawValue(),
        m_attackTarget.GetRawValue()};
    std::string const rule = m_strategies.Think(context);
    // A bot without a group has no chat session: its own coordinator drops
    // the events.
    if (!me->GetGroup())
        m_soloCoordinator.TakeEvents();
    if (!rule.empty() && rule != m_lastRule)
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s: %s", me->GetName(), rule.c_str());
    if (!rule.empty())
        m_lastRule = rule;

    // Out of combat, a bot that is not busy with a corpse puts on a clear
    // upgrade that it looted.
    if (me->IsAlive() && !me->IsInCombat() && !m_loot.HasWork(me, false))
        m_loot.PutOnUpgrades(me);
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
