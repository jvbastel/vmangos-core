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
#include "BossReactions.h"
#include "BotCommand.h"
#include "BotLoot.h"
#include "BotRecipe.h"
#include "CommandState.h"
#include "GroupCoordinator.h"
#include "Inbox.h"
#include "StartDelay.h"
#include "Strategy.h"
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
        BotAI(Bots::ThinkSettings const& thinkSettings, Bots::StartDelaySettings const& startDelay, Bots::BotRole role);

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
        // SMSG_PARTYKILLLOG: a boss kill of the group goes to the coordinator.
        void OnPartyKill(WorldPacket const& packet);
        void HandleCommand(BotCommandMessage const& message);
        void StartCommand(Player* player);
        // The start delay: begins and ends the waits of m_startGate.
        void UpdateStartDelay(uint32 diff);
        void StartPull(Player* player);
        Bots::PullTarget PullTargetOf(Player const* player) const;
        bool IsDamageBot() const;
        // Forgets the attack target when it died or the bot cannot see it.
        void CheckAttackTarget();
        bool IsInReach(Player const* player) const;
        // False without a group, or on another map than the group leader.
        bool UsesGroupCoordinator() const;
        // The coordinator of the bot's group, or the bot's own one.
        std::shared_ptr<Bots::GroupCoordinator> Coordinator();
        void Think();
        // Accepts the resurrect of SMSG_RESURRECT_REQUEST.
        void AcceptResurrect(WorldPacket const& packet);
        // True when a member of the bot's group on its map eats or drinks
        // after a fight: the group waits for it.
        bool GroupRests();
        bool IsNearPlayer() const;

        void KeepLevelCorrect();

        Bots::Inbox<WorldPacket> m_inbox;
        Bots::Inbox<BotCommandMessage> m_commands;
        Bots::CommandState m_commandState = Bots::CommandState::None;
        ObjectGuid m_commandPlayer; // The player of the last command that the bot obeyed.
        // The target of the player at the last `attack`, for a damage bot.
        // It ends when it dies or the bot cannot see it, and at `stop`.
        ObjectGuid m_attackTarget;
        Bots::ThinkTimer m_thinkTimer;
        Bots::StartDelaySettings m_startDelay;
        // The bot waits a random delay before it starts to move.
        Bots::StartGate m_startGate;
        BotLoot m_loot;
        Bots::BotRole m_role;
        // The active strategies. Made at the first think, from the class of
        // the player and the role of the bot.
        Bots::StrategyStack m_strategies;
        bool m_strategiesMade = false;
        // The boss reaction on m_strategies during a boss fight of the group.
        Bots::BossReactionSlot m_bossReaction;
        // The rule of the last think that did something, for the debug log.
        std::string m_lastRule;
        // The coordinator of a bot without a group, or away from its leader.
        Bots::GroupCoordinator m_soloCoordinator;
        // The bot is in a group but on another map than the leader: it
        // ignores the group coordinator. For the log line of a change.
        bool m_awayFromLeader = false;
        // The level routine ran after the login (and so after the creation).
        bool m_levelRoutineDone = false;
        // The pet that has its pet spells. Empty without a pet.
        ObjectGuid m_taughtPet;
};

#endif
