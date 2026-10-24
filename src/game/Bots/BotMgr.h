/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_BOTMGR_H
#define WOWAI_GAME_BOTMGR_H

#include "Common.h"
#include "Policies/Singleton.h"
#include "PlayerBotMgr.h"
#include "BotChat.h"
#include "BotGear.h"
#include "BotLevelRoutine.h"
#include "BotRecipe.h"
#include "GroupCoordinator.h"
#include "GroupCommand.h"
#include "Route.h"
#include "Inbox.h"
#include "ObjectGuid.h"
#include "ThinkTimer.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Group;

// The settings `Bots.*` of mangosd.conf.
struct BotSettings
{
    bool enable = true;
    std::string accountPrefix = "BOT";
    Bots::ThinkSettings think;
    std::string dataDir = "bots";
};

// One persistent bot, from a row of the `bot_character` table.
struct BotRecord
{
    // PlayerBotEntry, so that Player::IsBot() and the packet delivery of the
    // socketless session work. The entry is in this list only, never in the
    // list of PlayerBotMgr.
    std::shared_ptr<PlayerBotEntry> entry;
    std::string role;
    bool worldBot = false;
    // The gear template that the bot puts on when it is in the world, after
    // its creation.
    std::string pendingGear;
    // The player who invited the bot while it was offline: the bot joins the
    // group and comes to the player when it is in the world. Empty: none.
    ObjectGuid pendingInvite;
    // The bot was in a group at its last update. A bot whose group goes away
    // (a removal by the player, or the end of the group) logs out.
    bool wasInGroup = false;
};

// An `invite` or `dismiss` from the chat of a player.
struct GroupCommandMessage
{
    Bots::GroupCommandKind kind;
    ObjectGuid player;   // The player who gave the command.
    std::string botName; // As the player wrote it.
};

// A chat line that a bot says, for example a fixed line when it cannot obey a
// bot command.
struct BotLine
{
    ObjectGuid bot;
    uint32 chatType;     // CHAT_MSG_PARTY: to the party of the bot. CHAT_MSG_WHISPER: to `receiver`.
    ObjectGuid receiver; // Only for a whisper.
    std::string text;
};

// The bot manager of the WOWAI bots. It sits next to PlayerBotMgr and shares
// no state with it. It keeps the low-level login layer of the core (the
// socketless WorldSession, the PlayerAI hook, and the login of a character),
// and it logs the bots in and out. It runs on the world thread: World::Update
// updates it after the maps, and the GM commands call it from there too.
class BotMgr
{
    public:
        // The settings. At startup and at `.reload config`.
        void LoadConfig();
        // The bots of the `bot_character` table, all offline. At startup.
        void Load();
        // Logs in the bots whose session the world has added.
        void Update(uint32 diff);

        // Makes the character, its pool account if needed, and its
        // `bot_character` row, then logs the bot in. False with a short line
        // for the GM in `error`.
        bool CreateBot(Bots::BotRecipe const& recipe, std::string& error);
        bool LoginBot(std::string const& name, std::string& error);
        bool LogoutBot(std::string const& name, std::string& error);

        // From any thread (the bot AIs on the map threads): the bot says the
        // line at the next update of the bot manager.
        void QueueLine(BotLine line) { m_lines.Push(std::move(line)); }
        // From any thread (the chat handlers): the bot manager runs the
        // group command at its next update.
        void QueueGroupCommand(GroupCommandMessage message) { m_groupCommands.Push(std::move(message)); }

        // Equips a character in the world from a gear template, by name. With
        // no template name, a bot wears the generated template of its class
        // and role. Fills `report` with the picks.
        bool EquipCharacter(std::string const& name, std::string const& templateName, std::string& report, std::string& error);

        BotGear const& GetGear() const { return m_gear; }
        // The scores of the generated gear template of a bot's class and
        // role, for its loot decisions. Null for no bot or no template.
        Bots::GearScores const* GearScoresOf(Player const* bot) const;

        BotSettings const& GetSettings() const { return m_settings; }
        BotLevelRoutine const& GetLevelRoutine() const { return m_levelRoutine; }
        // Runs the level routine on a bot, with the talent build of its class
        // and role.
        void ApplyLevelRoutine(Player* bot) const;

        // True for a character that is one of our bots and online, not for
        // the bots of PlayerBotMgr.
        bool IsOurBot(Player const* player) const;
        // Runs the level routine after a level-up of one of our bots.
        void OnLevelUp(Player* player);

        // From any map thread: the group coordinator of the group with the
        // id `groupId`. The bot manager makes it at the first call, and drops
        // it when the group is gone.
        std::shared_ptr<Bots::GroupCoordinator> GetCoordinator(uint32 groupId);
        // Bot chat. Its QueuePlayerMessage works from any thread.
        BotChat& Chat() { return m_chat; }
        // For bot chat: the role of one of our bots that is online, or false.
        bool RoleOfOurBot(Player const* player, std::string& role) const;
        // True for a character of the `bot_character` table, online or not.
        bool IsBotCharacter(ObjectGuid guid) const;
        // The characters of our bots that are online.
        std::vector<ObjectGuid> OnlineBots() const;
        // From any map thread: the creature entry is in the boss list of a
        // route file of the dungeon map. The bot manager reads the route
        // files at startup only, so the bots can read the list without a
        // lock: a boss added with the route recorder counts after a restart.
        bool IsBoss(uint32 mapId, uint32 entry) const { return m_bosses.IsBoss(mapId, entry); }

    private:
        BotRecord* FindByName(std::string const& name);
        uint32 ChooseAccount(std::string& error);
        uint32 CreateAccount(std::string const& name, std::string& error);
        void StartLogin(BotRecord& record, WorldSession* session);
        void SendLines();
        void RunGroupCommand(GroupCommandMessage const& message);
        bool RunGroupPlan(Bots::GroupCommandKind kind, BotRecord* record, Player* player);
        void UpdateGroupOf(BotRecord& record);
        // The group has a member that is no WOWAI bot, online or not.
        bool HasPlayerMember(Group const& group) const;
        void PutOnPendingGear(BotRecord& record);
        // The boss lists of the route files in <DataDir>/routes.
        void LoadBossList();
        void DropOldCoordinators();
        // The wipe and rest moments that the coordinators saw since the last
        // update: one call of OnGroupEvent each.
        void TakeGroupEvents();
        // A group event of the group with the id `groupId`, once for the
        // group: a log line and its bot chat event.
        void OnGroupEvent(uint32 groupId, Bots::GroupEvent const& event);

        BotSettings m_settings;
        BotLevelRoutine m_levelRoutine;
        std::map<uint32 /*character GUID*/, BotRecord> m_bots;
        Bots::Inbox<BotLine> m_lines;
        Bots::Inbox<GroupCommandMessage> m_groupCommands;
        BotGear m_gear;
        // The bots of one group can sit on two map threads.
        std::mutex m_coordinatorsMutex;
        std::map<uint32 /*group id*/, std::shared_ptr<Bots::GroupCoordinator>> m_coordinators;
        BotChat m_chat;
        Bots::BossList m_bosses;
};

// The class of the library for a class of the game. False for an unknown class.
bool BotClassOf(uint8 gameClass, Bots::BotClass& botClass);

#define sBotMgr MaNGOS::Singleton<BotMgr>::Instance()

#endif
