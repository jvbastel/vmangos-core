/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "BotMgr.h"
#include "BotAI.h"
#include "BotAccounts.h"
#include "Policies/SingletonImp.h"
#include "AccountMgr.h"
#include "Config/Config.h"
#include "Crypto/Authentication/SRP6.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Utilities/Random.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <sstream>

INSTANTIATE_SINGLETON_1(BotMgr);

namespace
{
    uint8 GameRace(Bots::BotRace race)
    {
        switch (race)
        {
            case Bots::BotRace::Orc:
                return RACE_ORC;
            case Bots::BotRace::Undead:
                return RACE_UNDEAD;
            case Bots::BotRace::Tauren:
                return RACE_TAUREN;
            case Bots::BotRace::Troll:
                break;
        }
        return RACE_TROLL;
    }

    uint8 GameClass(Bots::BotClass botClass)
    {
        switch (botClass)
        {
            case Bots::BotClass::Warrior:
                return CLASS_WARRIOR;
            case Bots::BotClass::Priest:
                return CLASS_PRIEST;
            case Bots::BotClass::Rogue:
                return CLASS_ROGUE;
            case Bots::BotClass::Warlock:
                return CLASS_WARLOCK;
            case Bots::BotClass::Mage:
                return CLASS_MAGE;
            case Bots::BotClass::Hunter:
                return CLASS_HUNTER;
            case Bots::BotClass::Shaman:
                return CLASS_SHAMAN;
            case Bots::BotClass::Druid:
                break;
        }
        return CLASS_DRUID;
    }

    bool BotClassOf(uint8 gameClass, Bots::BotClass& botClass)
    {
        switch (gameClass)
        {
            case CLASS_WARRIOR: botClass = Bots::BotClass::Warrior; return true;
            case CLASS_PRIEST: botClass = Bots::BotClass::Priest; return true;
            case CLASS_ROGUE: botClass = Bots::BotClass::Rogue; return true;
            case CLASS_WARLOCK: botClass = Bots::BotClass::Warlock; return true;
            case CLASS_MAGE: botClass = Bots::BotClass::Mage; return true;
            case CLASS_HUNTER: botClass = Bots::BotClass::Hunter; return true;
            case CLASS_SHAMAN: botClass = Bots::BotClass::Shaman; return true;
            case CLASS_DRUID: botClass = Bots::BotClass::Druid; return true;
            default: return false;
        }
    }

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return text;
    }

    std::string RandomPassword()
    {
        static char const letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::string password;
        for (int i = 0; i < 16; ++i)
            password += letters[urand(0, sizeof(letters) - 2)];
        return password;
    }

    std::shared_ptr<PlayerBotEntry> NewEntry(uint32 guid, uint32 account, std::string const& name, BotSettings const& settings)
    {
        std::shared_ptr<PlayerBotEntry> entry = std::make_shared<PlayerBotEntry>(guid, account, 0);
        entry->name = name;
        entry->customBot = true;
        // Our bots are persistent characters: the core saves them like players.
        entry->allowSaving = true;
        entry->ai.reset(new BotAI(settings.think));
        entry->ai->botEntry = entry.get();
        return entry;
    }
}

void BotMgr::LoadConfig()
{
    m_settings.enable = sConfig.GetBoolDefault("Bots.Enable", true);
    m_settings.accountPrefix = sConfig.GetStringDefault("Bots.AccountPrefix", "BOT");
    m_settings.think.combatMs = sConfig.GetIntDefault("Bots.ThinkMs.Combat", 100);
    m_settings.think.nearPlayerMs = sConfig.GetIntDefault("Bots.ThinkMs.NearPlayer", 500);
    m_settings.think.farMs = sConfig.GetIntDefault("Bots.ThinkMs.Far", 3000);
    m_settings.dataDir = sConfig.GetStringDefault("Bots.DataDir", "bots");
    AccountMgr::normalizeString(m_settings.accountPrefix);

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Enable %u, accounts %s###, think %u/%u/%u ms, data %s",
        uint32(m_settings.enable), m_settings.accountPrefix.c_str(), m_settings.think.combatMs,
        m_settings.think.nearPlayerMs, m_settings.think.farMs, m_settings.dataDir.c_str());
}

void BotMgr::Load()
{
    m_levelRoutine.Load(m_settings.dataDir);
    m_bots.clear();
    std::unique_ptr<QueryResult> result = CharacterDatabase.Query(
        "SELECT `b`.`guid`, `b`.`account`, `b`.`role`, `b`.`world_bot`, `c`.`name`"
        " FROM `bot_character` `b` JOIN `characters` `c` ON `c`.`guid` = `b`.`guid`");
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            BotRecord record;
            record.entry = NewEntry(fields[0].GetUInt32(), fields[1].GetUInt32(), fields[4].GetCppString(), m_settings);
            record.role = fields[2].GetCppString();
            record.worldBot = fields[3].GetUInt8() != 0;
            m_bots[fields[0].GetUInt32()] = record;
        } while (result->NextRow());
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %u bots loaded", uint32(m_bots.size()));
    m_gear.Load(m_settings.dataDir + "/gear");
}

void BotMgr::Update(uint32 /*diff*/)
{
    for (auto& pair : m_bots)
    {
        PlayerBotEntry& entry = *pair.second.entry;
        if (entry.state == PB_STATE_ONLINE && !pair.second.pendingGear.empty())
            PutOnPendingGear(pair.second);
        if (entry.state == PB_STATE_ONLINE && !sWorld.FindSession(entry.accountId))
        {
            // The session ended, for example after a kick.
            entry.state = PB_STATE_OFFLINE;
            continue;
        }
        if (entry.state != PB_STATE_LOADING)
            continue;

        // The world adds new sessions at its next session update.
        WorldSession* session = sWorld.FindSession(entry.accountId);
        if (!session)
            continue;
        if (entry.ai->OnSessionLoaded(&entry, session))
        {
            entry.state = PB_STATE_ONLINE;
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %s logs in (GUID %u, account %u)", entry.name.c_str(), uint32(entry.playerGUID), entry.accountId);
        }
        else
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] %s cannot log in", entry.name.c_str());
            entry.state = PB_STATE_OFFLINE;
        }
    }
}

bool BotMgr::IsOurBot(Player const* player) const
{
    auto const record = m_bots.find(player->GetGUIDLow());
    return record != m_bots.end() && player->GetSession()->GetBot() == record->second.entry.get();
}

void BotMgr::OnLevelUp(Player* player)
{
    if (!IsOurBot(player))
        return;
    m_levelRoutine.Apply(player);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %s reaches level %u", player->GetName(), player->GetLevel());
}

BotRecord* BotMgr::FindByName(std::string const& name)
{
    std::string wanted = name;
    if (!normalizePlayerName(wanted))
        return nullptr;
    for (auto& pair : m_bots)
        if (pair.second.entry->name == wanted)
            return &pair.second;
    return nullptr;
}

uint32 BotMgr::CreateAccount(std::string const& name, std::string& error)
{
    // Like AccountMgr::CreateAccount, but at once: the bot needs the id now.
    std::string const password = RandomPassword();
    SRP6 srp;
    srp.CalculateVerifier(sAccountMgr.CalculateShaPassHash(name, password));
    if (!LoginDatabase.DirectPExecute("INSERT INTO `account` (`username`, `v`, `s`, `joindate`) VALUES ('%s', '%s', '%s', NOW())",
            name.c_str(), srp.GetVerifier().AsHexStr().c_str(), srp.GetSalt().AsHexStr().c_str()))
    {
        error = "Cannot make the bot account " + name + ".";
        return 0;
    }
    LoginDatabase.DirectExecute("REPLACE INTO `realmcharacters` (`realmid`, `acctid`, `numchars`) SELECT `realmlist`.`id`, `account`.`id`, 0"
        " FROM `realmlist`, `account` LEFT JOIN `realmcharacters` ON `acctid` = `account`.`id` WHERE `acctid` IS NULL");
    uint32 const id = sAccountMgr.GetId(name);
    if (!id)
        error = "Cannot find the new bot account " + name + ".";
    else
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Made the bot account %s (id %u)", name.c_str(), id);
    return id;
}

uint32 BotMgr::ChooseAccount(std::string& error)
{
    // The pool accounts, with their number of characters.
    std::string pattern = m_settings.accountPrefix;
    LoginDatabase.escape_string(pattern);
    std::map<uint32, Bots::BotAccount> accounts;
    std::unique_ptr<QueryResult> result = LoginDatabase.PQuery(
        "SELECT `id`, `username` FROM `account` WHERE `username` LIKE '%s%%'", pattern.c_str());
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            accounts[fields[0].GetUInt32()] = {fields[1].GetCppString(), 0};
        } while (result->NextRow());
    }
    if (!accounts.empty())
    {
        std::ostringstream ids;
        for (auto const& pair : accounts)
            ids << (pair.first == accounts.begin()->first ? "" : ",") << pair.first;
        result = CharacterDatabase.PQuery("SELECT `account`, COUNT(*) FROM `characters` WHERE `account` IN (%s) GROUP BY `account`", ids.str().c_str());
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                accounts[fields[0].GetUInt32()].characters = fields[1].GetUInt32();
            } while (result->NextRow());
        }
    }

    std::vector<Bots::BotAccount> pool;
    for (auto const& pair : accounts)
        pool.push_back(pair.second);
    Bots::AccountChoice choice = Bots::ChooseBotAccount(m_settings.accountPrefix, pool);
    switch (choice.kind)
    {
        case Bots::AccountChoice::UseExisting:
            for (auto const& pair : accounts)
                if (pair.second.name == choice.name)
                    return pair.first;
            error = "Cannot find the bot account " + choice.name + ".";
            return 0;
        case Bots::AccountChoice::Create:
            return CreateAccount(choice.name, error);
        case Bots::AccountChoice::PoolFull:
            break;
    }
    error = "All bot accounts are full.";
    return 0;
}

bool BotMgr::CreateBot(Bots::BotRecipe const& recipe, std::string& error)
{
    if (!m_settings.enable)
    {
        error = "The bots are off (Bots.Enable = 0).";
        return false;
    }

    std::string name = recipe.name;
    if (!normalizePlayerName(name) || ObjectMgr::CheckPlayerName(name, true) != CHAR_NAME_SUCCESS)
    {
        error = "'" + recipe.name + "' is not a valid character name.";
        return false;
    }
    if (!sObjectMgr.GetPlayerGuidByName(name).IsEmpty())
    {
        error = "A character named " + name + " exists already.";
        return false;
    }

    uint8 const race = GameRace(recipe.race);
    uint8 const botClass = GameClass(recipe.botClass);
    if (!sObjectMgr.GetPlayerInfo(race, botClass))
    {
        error = "The world database has no data for this race and class.";
        return false;
    }

    // The gear template: a named one must exist. With no name, the bot
    // keeps its starting gear until a generated template exists.
    std::string const gearName = recipe.gearTemplate.empty()
        ? Bots::GeneratedGearTemplateName(recipe.botClass, recipe.role) : recipe.gearTemplate;
    if (!recipe.gearTemplate.empty() && !m_gear.Find(gearName))
    {
        error = "No gear template is named " + gearName + ".";
        return false;
    }

    uint32 const accountId = ChooseAccount(error);
    if (!accountId)
        return false;

    uint8 const gender = urand(0, 1) ? GENDER_FEMALE : GENDER_MALE;
    uint8 skin, face, hairStyle, hairColor, facialHair;
    Player::SelectRandomAppearance(race, gender, hairStyle, hairColor, face, facialHair, skin);

    // The session of the bot. It makes the character, then logs it in.
    WorldSession* session = new WorldSession(accountId, nullptr, SEC_PLAYER, 0, LOCALE_enUS);
    uint32 const guid = sObjectMgr.GeneratePlayerLowGuid();
    if (!Player::SaveNewPlayer(session, guid, name, race, botClass, gender, skin, face, hairStyle, hairColor, facialHair))
    {
        delete session;
        error = "Cannot make the character " + name + ".";
        return false;
    }

    // The same serial id as the character insert and the login queries, so
    // that the database runs them in this order. Health and mana are capped
    // to the maximum of the level at login.
    std::string role = Bots::RoleName(recipe.role);
    CharacterDatabase.BeginTransaction(guid);
    CharacterDatabase.PExecute("UPDATE `characters` SET `level` = %u, `xp` = 0, `health` = 1000000, `power1` = 1000000 WHERE `guid` = %u",
        recipe.level, guid);
    CharacterDatabase.PExecute("INSERT INTO `bot_character` (`guid`, `account`, `role`, `world_bot`) VALUES (%u, %u, '%s', 0)",
        guid, accountId, role.c_str());
    CharacterDatabase.CommitTransaction();
    sWorld.UpdateRealmCharCount(accountId);

    BotRecord& record = m_bots[guid];
    record.entry = NewEntry(guid, accountId, name, m_settings);
    record.role = role;
    record.worldBot = false;
    if (m_gear.Find(gearName))
        record.pendingGear = gearName;
    else
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] No gear template %s: %s keeps its starting gear", gearName.c_str(), name.c_str());
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Made the bot %s (GUID %u, account %u, %s)", name.c_str(), guid, accountId, role.c_str());

    StartLogin(record, session);
    return true;
}

bool BotMgr::LoginBot(std::string const& name, std::string& error)
{
    BotRecord* record = FindByName(name);
    if (!record)
    {
        error = "No bot is named " + name + ".";
        return false;
    }
    if (record->entry->state != PB_STATE_OFFLINE)
    {
        error = record->entry->name + " is online already.";
        return false;
    }
    if (sWorld.FindSession(record->entry->accountId))
    {
        error = "The account of " + record->entry->name + " is in use.";
        return false;
    }
    uint32 const account = record->entry->accountId;
    StartLogin(*record, new WorldSession(account, nullptr, sAccountMgr.GetSecurity(account), 0, LOCALE_enUS));
    return true;
}

bool BotMgr::LogoutBot(std::string const& name, std::string& error)
{
    BotRecord* record = FindByName(name);
    if (!record)
    {
        error = "No bot is named " + name + ".";
        return false;
    }
    if (record->entry->state == PB_STATE_OFFLINE)
    {
        error = record->entry->name + " is offline already.";
        return false;
    }
    // WorldSession::Update logs out the player of an offline bot entry and
    // ends the session.
    record->entry->state = PB_STATE_OFFLINE;
    return true;
}

void BotMgr::StartLogin(BotRecord& record, WorldSession* session)
{
    // A new AI each time, with an empty inbox and a fresh think timer.
    record.entry->ai.reset(new BotAI(m_settings.think));
    record.entry->ai->botEntry = record.entry.get();
    record.entry->state = PB_STATE_LOADING;
    session->SetBot(record.entry);
    sWorld.AddSession(session);
}

void BotMgr::PutOnPendingGear(BotRecord& record)
{
    // By GUID: the session of the account can hold another character.
    Player* player = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, uint32(record.entry->playerGUID)));
    if (!player || !player->IsInWorld())
        return;
    if (Bots::GearTemplate const* gear = m_gear.Find(record.pendingGear))
        EquipFromGearTemplate(player, *gear);
    record.pendingGear.clear();
    // Then the level routine, for the weapon skills of the new gear.
    m_levelRoutine.Apply(player);
}

bool BotMgr::EquipCharacter(std::string const& name, std::string const& templateName, std::string& report, std::string& error)
{
    std::string wanted = name;
    Player* player = normalizePlayerName(wanted) ? sObjectMgr.GetPlayer(wanted.c_str()) : nullptr;
    if (!player || !player->IsInWorld())
    {
        error = "No character named " + name + " is in the world.";
        return false;
    }

    std::string gearName = Lower(templateName);
    if (gearName.empty())
    {
        auto found = m_bots.find(player->GetGUIDLow());
        Bots::BotClass botClass;
        Bots::BotRole role;
        if (found == m_bots.end() || !BotClassOf(player->GetClass(), botClass) || !Bots::ParseRoleName(found->second.role, role))
        {
            error = wanted + " is not a bot. Name a gear template.";
            return false;
        }
        gearName = Bots::GeneratedGearTemplateName(botClass, role);
    }
    Bots::GearTemplate const* gear = m_gear.Find(gearName);
    if (!gear)
    {
        error = "No gear template is named " + gearName + ".";
        return false;
    }

    Bots::GearDraw const draw = EquipFromGearTemplate(player, *gear);
    // A worn weapon without its skill leaves its slot at the next login.
    if (IsOurBot(player))
        m_levelRoutine.Apply(player);
    report = wanted + " wears " + gear->name + (draw.shape.empty() ? "" : " (shape " + draw.shape + ")") + ": " +
        (draw.picks.empty() ? std::string("nothing") : DescribeGearDraw(draw));
    return true;
}
