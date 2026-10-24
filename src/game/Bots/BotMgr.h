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
#include "BotGear.h"
#include "BotLevelRoutine.h"
#include "BotRecipe.h"
#include "GroupCoordinator.h"
#include "Inbox.h"
#include "ObjectGuid.h"
#include "ThinkTimer.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

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

        // Equips a character in the world from a gear template, by name. With
        // no template name, a bot wears the generated template of its class
        // and role. Fills `report` with the picks.
        bool EquipCharacter(std::string const& name, std::string const& templateName, std::string& report, std::string& error);

        BotGear const& GetGear() const { return m_gear; }

        BotSettings const& GetSettings() const { return m_settings; }
        BotLevelRoutine const& GetLevelRoutine() const { return m_levelRoutine; }

        // True for a character that is one of our bots and online, not for
        // the bots of PlayerBotMgr.
        bool IsOurBot(Player const* player) const;
        // Runs the level routine after a level-up of one of our bots.
        void OnLevelUp(Player* player);

        // From any map thread: the group coordinator of the group with the
        // id `groupId`. The bot manager makes it at the first call, and drops
        // it when the group is gone.
        std::shared_ptr<Bots::GroupCoordinator> GetCoordinator(uint32 groupId);

    private:
        BotRecord* FindByName(std::string const& name);
        uint32 ChooseAccount(std::string& error);
        uint32 CreateAccount(std::string const& name, std::string& error);
        void StartLogin(BotRecord& record, WorldSession* session);
        void SendLines();
        void PutOnPendingGear(BotRecord& record);
        void DropOldCoordinators();

        BotSettings m_settings;
        BotLevelRoutine m_levelRoutine;
        std::map<uint32 /*character GUID*/, BotRecord> m_bots;
        Bots::Inbox<BotLine> m_lines;
        BotGear m_gear;
        // The bots of one group can sit on two map threads.
        std::mutex m_coordinatorsMutex;
        std::map<uint32 /*group id*/, std::shared_ptr<Bots::GroupCoordinator>> m_coordinators;
};

// The class of the library for a class of the game. False for an unknown class.
bool BotClassOf(uint8 gameClass, Bots::BotClass& botClass);

#define sBotMgr MaNGOS::Singleton<BotMgr>::Instance()

#endif
