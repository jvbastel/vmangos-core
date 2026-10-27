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
#include "CoordinatorChoice.h"
#include "DeathAndRest.h"
#include "MemberSpot.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "SpellMgr.h"
#include "TaragamanReaction.h"
#include "Trail.h"
#include "Pet.h"
#include "Player.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
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
    // A spline this short is a turn on the spot, not a walk.
    int32 const MaxTurnSplineMs = 50;

    // The clock of the group events of the coordinator.
    int64 SteadyMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

BotAI::BotAI(Bots::ThinkSettings const& thinkSettings, Bots::StartDelaySettings const& startDelay, Bots::BotRole role) :
    PlayerBotAI(nullptr), m_thinkTimer(thinkSettings), m_startDelay(startDelay), m_role(role)
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
    // The opcode first: most packets are of no use to the bot. The combat
    // log counts only in a scripted run (OnSpellMissLog, OnSpellFailedOther,
    // OnSpellDamageLog).
    uint16 const opcode = packet->GetOpcode();
    bool const runLog = (opcode == SMSG_SPELLLOGMISS || opcode == SMSG_SPELL_FAILED_OTHER ||
        opcode == SMSG_SPELLNONMELEEDAMAGELOG) && sBotMgr.HasTestDriver();
    if (!IsInboxOpcode(opcode) && !runLog)
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
    UpdateStartDelay(diff);

    Bots::ThinkState state = Bots::ThinkStateFor(me->IsInCombat(), IsNearPlayer());
    if (m_thinkTimer.Advance(diff, state))
        Think();

    // At each update, not only at the think: the group sees a meal start or
    // end at once, so no pull goes out while a member sits down to eat.
    if (me->GetGroup())
    {
        BotAdapter adapter(me);
        Bots::WatchRest(adapter, *Coordinator());
    }
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
            // one `loot_roll` event of it. A bot away from the leader ignores
            // the group coordinator.
            BotLootRollWon const won = m_loot.OnRollWon(me, packet);
            if (UsesGroupCoordinator())
                Coordinator()->NoteRollWon(won.lootedTarget.GetRawValue(), won.itemSlot,
                    won.itemEntry, won.winner.GetRawValue(), SteadyMs());
            return;
        }
        case SMSG_PARTYKILLLOG:
            OnPartyKill(packet);
            return;
        case SMSG_SPELLLOGMISS:
            OnSpellMissLog(packet);
            return;
        case SMSG_SPELL_FAILED_OTHER:
            OnSpellFailedOther(packet);
            return;
        case SMSG_SPELLNONMELEEDAMAGELOG:
            OnSpellDamageLog(packet);
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
    if (!UsesGroupCoordinator() || !victim.IsCreature() || !sBotMgr.IsBoss(me->GetMapId(), victim.GetEntry()))
        return;
    Coordinator()->NoteBossKill(victim.GetRawValue(), victim.GetEntry(), SteadyMs());
}

void BotAI::OnSpellMissLog(WorldPacket const& packet)
{
    // An interrupt of this bot that missed (a dodge, a parry), in a scripted
    // run: another bot may try the cast again (spec section 10, Results).
    if (!sBotMgr.IsScriptedRunGroup(me->GetGroup()))
        return;
    WorldPacket data(packet);
    uint32 spell = 0, count = 0;
    ObjectGuid caster;
    uint8 extended = 0;
    data >> spell >> caster >> extended >> count;
    SpellEntry const* entry = sSpellMgr.GetSpellEntry(spell);
    if (caster != me->GetObjectGuid() || !entry || !entry->HasEffect(SPELL_EFFECT_INTERRUPT_CAST))
        return;
    for (uint32 i = 0; i < count; ++i)
    {
        ObjectGuid target;
        uint8 missInfo = 0;
        data >> target >> missInfo;
        if (extended)
            data.read_skip(2 * sizeof(float));
        sBotMgr.Run().QueueCombatNote({RunCombatNote::Kind::InterruptMissed, me->GetObjectGuid(), target, 0, 0});
    }
}

void BotAI::OnSpellFailedOther(WorldPacket const& packet)
{
    // An enemy cast that stopped before it went off, in a scripted run: an
    // interrupt, a stun, or the death of the caster. Each member near gets
    // it; the run counts it once (spec section 10, Results).
    if (!sBotMgr.IsScriptedRunGroup(me->GetGroup()))
        return;
    WorldPacket data(packet);
    ObjectGuid caster;
    uint32 spell = 0;
    data >> caster >> spell;
    if (!caster.IsCreature())
        return;
    sBotMgr.Run().QueueCombatNote({RunCombatNote::Kind::CastStopped, me->GetObjectGuid(), caster, spell, 0});
}

void BotAI::OnSpellDamageLog(WorldPacket const& packet)
{
    // A Fire Nova of Taragaman that hit this bot, in a scripted run (spec
    // section 10, Results). Each member near gets the log; only the target
    // notes it.
    if (!sBotMgr.IsScriptedRunGroup(me->GetGroup()))
        return;
    WorldPacket data(packet);
    ObjectGuid target, attacker;
    uint32 spell = 0, damage = 0;
    data >> target.ReadAsPackedClientBuildAware() >> attacker.ReadAsPackedClientBuildAware() >> spell >> damage;
    if (target != me->GetObjectGuid() || spell != Bots::Taragaman::FireNova || attacker.GetEntry() != Bots::Taragaman::Entry)
        return;
    sBotMgr.Run().QueueCombatNote({RunCombatNote::Kind::FireNovaHit, me->GetObjectGuid(), attacker, spell, damage});
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
        sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] %s refuses %s: %s", me->GetName(),
            Bots::BotCommandWord(message.kind), outcome.line);
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
    if (m_commandState == Bots::CommandState::Follow || m_commandState == Bots::CommandState::Come)
    {
        // The bot starts when its delay ends (UpdateStartDelay).
        uint32 const delay = m_startGate.OnMoveCommand(m_startDelay, sBotMgr.Random());
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s waits %u ms before it moves (%s)", me->GetName(), delay,
            Bots::StartReasonName(Bots::StartReason::Command));
        return;
    }
    // `stop` and `stay` act at once.
    m_startGate.Cancel();
    StartCommand(player);
}

void BotAI::UpdateStartDelay(uint32 diff)
{
    Player* player = m_commandPlayer.IsEmpty() ? nullptr : me->GetMap()->GetPlayer(m_commandPlayer);
    Bots::StartFacts facts;
    facts.command = m_commandState;
    facts.inCombat = me->IsInCombat();
    facts.followsPlayer = player && BotAdapter(me).GetFollowTarget() == player->GetObjectGuid().GetRawValue();
    // A turn to face a spell target is a spline of about 1 ms: the bot
    // still stands.
    facts.botStands = me->movespline->Finalized() || me->movespline->Duration() <= MaxTurnSplineMs;
    facts.playerMoves = player && player->IsMoving();
    Bots::StartGate::Result const result = m_startGate.Update(diff, facts, m_startDelay, sBotMgr.Random());
    switch (result.step)
    {
        case Bots::StartGate::Step::None:
            return;
        case Bots::StartGate::Step::Wait:
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s waits %u ms before it moves (%s)", me->GetName(),
                result.delayMs, Bots::StartReasonName(result.reason));
            if (result.reason == Bots::StartReason::PlayerMoves)
            {
                // The follow movement would start at once: the bot stands
                // still until its delay ends.
                me->GetMotionMaster()->Clear();
                me->StopMoving();
                me->GetMotionMaster()->MoveIdle();
            }
            return;
        case Bots::StartGate::Step::Start:
            break;
    }
    if (result.reason == Bots::StartReason::Command)
    {
        // The `follow` or `come` of the player, if it still holds.
        if (player && IsInReach(player)
            && (m_commandState == Bots::CommandState::Follow || m_commandState == Bots::CommandState::Come))
            StartCommand(player);
        return;
    }
    if (result.reason == Bots::StartReason::PlayerMoves)
    {
        // The bot followed before its wait: it follows again.
        if (player && IsInReach(player) && m_commandState == Bots::CommandState::Follow)
            BotAdapter(me).Follow(player->GetObjectGuid().GetRawValue());
        return;
    }
    // Back to follow after a fight: the follow rule decides now, so the bot
    // does not wait for its next think too.
    if (me->IsAlive())
        Think();
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

Bots::CoordinatorChoice BotAI::CoordinatorChoiceNow() const
{
    Group const* group = me->GetGroup();
    return Bots::ChooseCoordinator(group != nullptr, BotPlace(), PlaceOfLeader());
}

Bots::MapPlace BotAI::BotPlace() const
{
    return {me->GetMapId(), me->GetInstanceId()};
}

Bots::LeaderPlace BotAI::PlaceOfLeader() const
{
    Group const* group = me->GetGroup();
    if (!group)
        return Bots::LeaderPlace::Unknown();
    // The leader can sit on another map thread: only its map and instance
    // ids are read.
    Player const* player = ObjectAccessor::FindPlayer(group->GetLeaderGuid());
    return player ? Bots::LeaderPlace::On({player->GetMapId(), player->GetInstanceId()}) : Bots::LeaderPlace::Unknown();
}

bool BotAI::UsesGroupCoordinator() const
{
    return CoordinatorChoiceNow() == Bots::CoordinatorChoice::Group;
}

std::shared_ptr<Bots::GroupCoordinator> BotAI::Coordinator()
{
    if (Group const* group = me->GetGroup())
        return sBotMgr.GetCoordinator(group->GetId(), BotPlace(), PlaceOfLeader(), SteadyMs());
    // No owner: the coordinator of a bot without a group is a member of this
    // AI.
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
        // stays in the list of the coordinator until the run forgets it
        // (BotRun::GroupResting).
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
            if (std::shared_ptr<Bots::Strategy const> spot = Bots::MakeMemberSpotStrategy(m_role))
                m_strategies.Push(spot);
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
    bool const away = CoordinatorChoiceNow() == Bots::CoordinatorChoice::Place;
    if (away != m_awayFromLeader)
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s uses the %s coordinator (map %u, instance %u)", me->GetName(),
            away ? "place" : "group", me->GetMapId(), me->GetInstanceId());
    m_awayFromLeader = away;
    // For the chase limit of the damage bots (GroupRules.h).
    // And the tank's trail, the cleared way of its fight spot (Trail.h).
    if (m_role == Bots::BotRole::Tank)
    {
        coordinator->SetTank(me->GetObjectGuid().GetRawValue());
        Bots::WatchTrail(adapter, *coordinator);
    }
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
    Bots::BotContext context = {adapter, adapter, *coordinator, m_commandState, m_commandPlayer.GetRawValue(),
        m_attackTarget.GetRawValue()};
    context.startWaits = m_startGate.Waiting();
    context.nowMs = SteadyMs();
    std::string const rule = m_strategies.Think(context);
    // A bot without a group has no chat session: its own coordinator drops
    // the events. The bot manager drops the events of a place coordinator.
    if (coordinator.get() == &m_soloCoordinator)
        m_soloCoordinator.TakeEvents();
    if (!rule.empty() && rule != m_lastRule)
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s: %s", me->GetName(), rule.c_str());
    if (!rule.empty())
        m_lastRule = rule;
    // The fight spot of the tank's pull (FightSpot.h), for the log.
    if (rule == "note the pull spot")
    {
        Bots::PullState const pull = coordinator->GetPull();
        if (pull.hasShootSpot)
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s fights at %.2f %.2f %.2f, shoots from %.2f %.2f %.2f (trail %u points)",
                me->GetName(), pull.spot.x, pull.spot.y, pull.spot.z, pull.shootSpot.x, pull.shootSpot.y,
                pull.shootSpot.z, uint32(coordinator->GetTrail().size()));
        else
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s fights at %.2f %.2f %.2f (trail %u points)", me->GetName(),
                pull.spot.x, pull.spot.y, pull.spot.z, uint32(coordinator->GetTrail().size()));
    }

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
        if (player && player != me && (!player->IsBot() || sBotMgr.IsTestDriver(player->GetObjectGuid())) &&
            player->IsWithinDist(me, NearPlayerYards))
            return true;
    }
    return false;
}
