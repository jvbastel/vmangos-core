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
#include "BotRecipe.h"
#include "ThinkTimer.h"

#include <map>
#include <memory>
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

        BotSettings const& GetSettings() const { return m_settings; }

    private:
        BotRecord* FindByName(std::string const& name);
        uint32 ChooseAccount(std::string& error);
        uint32 CreateAccount(std::string const& name, std::string& error);
        void StartLogin(BotRecord& record, WorldSession* session);

        BotSettings m_settings;
        std::map<uint32 /*character GUID*/, BotRecord> m_bots;
};

#define sBotMgr MaNGOS::Singleton<BotMgr>::Instance()

#endif
