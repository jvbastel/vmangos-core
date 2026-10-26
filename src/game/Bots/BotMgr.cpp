/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "BotMgr.h"
#include "GhostWalk.h"
#include "BotAI.h"
#include "BotAccounts.h"
#include "Chat.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Policies/SingletonImp.h"
#include "AccountMgr.h"
#include "Config/Config.h"
#include "Crypto/Authentication/SRP6.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RouteFiles.h"
#include "RouteTraces.h"
#include "SharedDefines.h"
#include "Utilities/Random.h"
#include "World.h"
#include "IO/Filesystem/FileSystem.h"
#include <fstream>
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <sstream>

INSTANTIATE_SINGLETON_1(BotMgr);

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

    Bots::BotRole RoleOf(std::string const& role)
    {
        Bots::BotRole botRole = Bots::BotRole::Melee;
        Bots::ParseRoleName(role, botRole);
        return botRole;
    }

    std::shared_ptr<PlayerBotEntry> NewEntry(uint32 guid, uint32 account, std::string const& name, std::string const& role, BotSettings const& settings)
    {
        std::shared_ptr<PlayerBotEntry> entry = std::make_shared<PlayerBotEntry>(guid, account, 0);
        entry->name = name;
        entry->customBot = true;
        // Our bots are persistent characters: the core saves them like players.
        entry->allowSaving = true;
        entry->ai.reset(new BotAI(settings.think, settings.startDelay, RoleOf(role)));
        entry->ai->botEntry = entry.get();
        return entry;
    }

    Player* BotPlayer(BotRecord const& record)
    {
        // By GUID: the session of the account can hold another character.
        return ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, uint32(record.entry->playerGUID)));
    }

    Bots::GroupFacts GroupFactsFor(BotRecord const* record, Player* player)
    {
        Bots::GroupFacts facts = {};
        Group* group = player->GetGroup();
        facts.playerMayInvite = !group || group->IsLeader(player->GetObjectGuid()) || group->IsAssistant(player->GetObjectGuid());
        facts.groupFull = group && group->IsFull();
        if (!record)
            return facts;

        ObjectGuid const botGuid(HIGHGUID_PLAYER, uint32(record->entry->playerGUID));
        Player* bot = BotPlayer(*record);
        // An offline bot keeps its group in the database.
        Group* botGroup = bot ? bot->GetGroup() : sObjectMgr.GetGroupByMember(botGuid);
        facts.botKnown = true;
        facts.botOnline = record->entry->state == PB_STATE_ONLINE && bot;
        facts.botInPlayerGroup = group && group->IsMember(botGuid);
        facts.botInOtherGroup = botGroup && botGroup != group;
        return facts;
    }

    void TellPlayer(Player* player, char const* line)
    {
        ChatHandler(player->GetSession()).SendSysMessage(line);
    }
}

void BotMgr::LoadConfig()
{
    m_settings.enable = sConfig.GetBoolDefault("Bots.Enable", true);
    m_settings.accountPrefix = sConfig.GetStringDefault("Bots.AccountPrefix", "BOT");
    m_settings.think.combatMs = sConfig.GetIntDefault("Bots.ThinkMs.Combat", 100);
    m_settings.think.nearPlayerMs = sConfig.GetIntDefault("Bots.ThinkMs.NearPlayer", 500);
    m_settings.think.farMs = sConfig.GetIntDefault("Bots.ThinkMs.Far", 3000);
    m_settings.startDelay.minMs = sConfig.GetIntDefault("Bots.StartDelayMs.Min", 300);
    m_settings.startDelay.maxMs = sConfig.GetIntDefault("Bots.StartDelayMs.Max", 1500);
    m_settings.dataDir = sConfig.GetStringDefault("Bots.DataDir", "bots");
    m_settings.lostMobMs = std::max(1000, sConfig.GetIntDefault("Bots.Run.LostMobMs", int(Bots::LostMobMs)));
    m_settings.runDir = sConfig.GetStringDefault("Bots.Run.ResultDir", "");
    if (m_settings.runDir.empty())
    {
        // The same folder as Log.cpp: a mounted folder of the realm.
        std::string logs = sConfig.GetStringDefault("LogsDir", "");
        if (!logs.empty() && logs.back() != '/' && logs.back() != '\\')
            logs += '/';
        m_settings.runDir = logs + "runs";
    }
    AccountMgr::normalizeString(m_settings.accountPrefix);
    m_chat.LoadConfig();

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Enable %u, accounts %s###, think %u/%u/%u ms, start delay %u-%u ms, data %s",
        uint32(m_settings.enable), m_settings.accountPrefix.c_str(), m_settings.think.combatMs,
        m_settings.think.nearPlayerMs, m_settings.think.farMs, m_settings.startDelay.minMs,
        m_settings.startDelay.maxMs, m_settings.dataDir.c_str());
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
            record.entry = NewEntry(fields[0].GetUInt32(), fields[1].GetUInt32(), fields[4].GetCppString(), fields[2].GetCppString(), m_settings);
            record.role = fields[2].GetCppString();
            record.worldBot = fields[3].GetUInt8() != 0;
            m_bots[fields[0].GetUInt32()] = record;
        } while (result->NextRow());
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %u bots loaded", uint32(m_bots.size()));
    m_gear.Load(m_settings.dataDir + "/gear");
    LoadBossList();
    m_chat.Start();
}

void BotMgr::Update(uint32 diff)
{
    // After the maps: the lines of bot chat whose delay ended.
    for (BotLine& line : m_chat.Update())
        m_lines.Push(std::move(line));
    SendLines();
    DropOldCoordinators();
    TakeGroupEvents();
    // The chat of a scripted run: the events sent (the player messages of
    // the update, and the group events above), and the answers.
    for (SentChatEvent const& sent : m_chat.TakeSent())
        m_run.NoteChatSent(sent);
    for (Bots::ChatEventOutcome const& outcome : m_chat.TakeOutcomes())
        m_run.NoteChatOutcome(outcome);
    for (GroupCommandMessage const& message : m_groupCommands.Drain())
        RunGroupCommand(message);
    m_run.Update();
    RouteTraces::Update(diff);
    for (auto& pair : m_bots)
    {
        PlayerBotEntry& entry = *pair.second.entry;
        if (entry.state == PB_STATE_ONLINE)
            UpdateGroupOf(pair.second);
        if (entry.state == PB_STATE_OFFLINE)
            pair.second.pendingInvite.Clear();
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

std::shared_ptr<Bots::GroupCoordinator> BotMgr::GetCoordinator(uint32 groupId)
{
    std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
    std::shared_ptr<Bots::GroupCoordinator>& coordinator = m_coordinators[groupId];
    if (!coordinator)
        coordinator = std::make_shared<Bots::GroupCoordinator>();
    return coordinator;
}

void BotMgr::DropCoordinator(uint32 groupId)
{
    std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
    m_coordinators.erase(groupId);
}

BotOnlineState BotMgr::OnlineStateOf(std::string const& name)
{
    BotRecord* record = FindByName(name);
    if (!record)
        return BotOnlineState::Unknown;
    PlayerBotEntry const& entry = *record->entry;
    if (entry.state == PB_STATE_OFFLINE)
        return sWorld.FindSession(entry.accountId) ? BotOnlineState::Busy : BotOnlineState::Offline;
    Player* player = BotPlayer(*record);
    return entry.state == PB_STATE_ONLINE && player && player->IsInWorld() ? BotOnlineState::Online : BotOnlineState::Busy;
}

void BotMgr::DropOldCoordinators()
{
    // On the world thread, after the maps. A bot holds its coordinator only
    // during its think, so a group that is gone frees its coordinator here.
    std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
    for (auto it = m_coordinators.begin(); it != m_coordinators.end();)
    {
        if (!sObjectMgr.GetGroupById(it->first))
            it = m_coordinators.erase(it);
        else
            ++it;
    }
}

void BotMgr::TakeGroupEvents()
{
    std::vector<std::pair<uint32, Bots::GroupEvent>> events;
    {
        std::lock_guard<std::mutex> lock(m_coordinatorsMutex);
        for (auto const& pair : m_coordinators)
            for (Bots::GroupEvent const& event : pair.second->TakeEvents())
                events.emplace_back(pair.first, event);
    }
    for (auto const& pair : events)
        OnGroupEvent(pair.first, pair.second);
}

void BotMgr::LoadBossList()
{
    std::string const dir = m_settings.dataDir + "/routes";
    m_bosses = Bots::BossList();
    m_ghostRoutes.clear();
    uint32 routes = 0;
    for (std::string const& file : IO::Filesystem::GetAllFilesInFolder(dir, IO::Filesystem::OutputFilePath::JustFileName))
    {
        if (RouteFiles::KindOf(file) != RouteFiles::FileKind::Route)
            continue;
        std::string const path = dir + "/" + file;
        std::ifstream in(path, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        Bots::RouteResult const result = Bots::ParseRoute(text.str());
        if (!in || !result.ok)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] Route %s: %s", path.c_str(), in ? result.error.c_str() : "cannot read the file");
            continue;
        }
        m_bosses.Add(result.route);
        uint32 dungeon = 0;
        if (Bots::IsGhostRouteName(result.route.name, dungeon) && !result.route.points.empty())
            m_ghostRoutes[dungeon] = result.route;
        ++routes;
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %u route files loaded for the boss lists, %u ghost routes", routes,
        uint32(m_ghostRoutes.size()));
}

Bots::Route const* BotMgr::GhostRoute(uint32 dungeonMap) const
{
    auto const found = m_ghostRoutes.find(dungeonMap);
    return found == m_ghostRoutes.end() ? nullptr : &found->second;
}

void BotMgr::OnGroupEvent(uint32 groupId, Bots::GroupEvent const& event)
{
    m_chat.PostGroupEvent(groupId, event);
    if (event.kind == Bots::GroupEventKind::Wipe)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] Group %u wipes", groupId);
        return;
    }
    if (event.kind == Bots::GroupEventKind::BossKill)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] Group %u kills the boss %u", groupId, event.entry);
        m_run.NoteBossKill(groupId, event.entry);
        return;
    }
    if (event.kind == Bots::GroupEventKind::LootRoll)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] Group %u: %s wins the roll on %u", groupId,
            ObjectGuid(event.winner).GetString().c_str(), event.entry);
        return;
    }
    char const* reason = "releases to run back";
    if (event.reason == Bots::RestReason::Eat)
        reason = "eats";
    else if (event.reason == Bots::RestReason::Drink)
        reason = "drinks";
    auto found = m_bots.find(ObjectGuid(event.bot).GetCounter());
    std::string const name = found != m_bots.end() ? found->second.entry->name : ObjectGuid(event.bot).GetString();
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] Group %u rests: %s %s", groupId, name.c_str(), reason);
}

bool BotMgr::IsOurBot(Player const* player) const
{
    auto const record = m_bots.find(player->GetGUIDLow());
    return record != m_bots.end() && player->GetSession()->GetBot() == record->second.entry.get();
}

bool BotMgr::RoleOfOurBot(Player const* player, std::string& role) const
{
    // Bot chat sees the test driver as the player.
    if (!IsOurBot(player) || IsTestDriver(player->GetObjectGuid()))
        return false;
    role = m_bots.find(player->GetGUIDLow())->second.role;
    return true;
}

bool BotMgr::IsScriptedRunGroup(Group const* group) const
{
    return group && IsTestDriver(group->GetLeaderGuid());
}

bool BotMgr::BotRoleOf(Player const* player, Bots::BotRole& role) const
{
    std::string name;
    return RoleOfOurBot(player, name) && Bots::ParseRoleName(name, role);
}

bool BotMgr::IsBotCharacter(ObjectGuid guid) const
{
    return guid.IsPlayer() && m_bots.count(guid.GetCounter()) != 0;
}

std::vector<ObjectGuid> BotMgr::OnlineBots() const
{
    std::vector<ObjectGuid> guids;
    for (auto const& pair : m_bots)
        if (pair.second.entry->state == PB_STATE_ONLINE)
            guids.push_back(ObjectGuid(HIGHGUID_PLAYER, pair.first));
    return guids;
}

void BotMgr::ApplyLevelRoutine(Player* bot) const
{
    std::string talentBuild;
    auto const record = m_bots.find(bot->GetGUIDLow());
    Bots::BotClass botClass;
    Bots::BotRole role;
    if (record != m_bots.end() && BotClassOf(bot->GetClass(), botClass) && Bots::ParseRoleName(record->second.role, role))
        talentBuild = Bots::TalentBuildName(botClass, role);
    m_levelRoutine.Apply(bot, talentBuild);
}

Bots::GearScores const* BotMgr::GearScoresOf(Player const* bot) const
{
    auto const record = m_bots.find(bot->GetGUIDLow());
    Bots::BotClass botClass;
    Bots::BotRole role;
    if (record == m_bots.end() || !BotClassOf(bot->GetClass(), botClass) || !Bots::ParseRoleName(record->second.role, role))
        return nullptr;
    return m_gear.FindScores(Bots::GeneratedGearTemplateName(botClass, role));
}

void BotMgr::OnLevelUp(Player* player)
{
    if (!IsOurBot(player))
        return;
    ApplyLevelRoutine(player);
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
    // The character of a bot made just now may not be saved yet: the bots
    // that this manager knows count too, so that back-to-back creates get
    // two accounts.
    std::map<std::string, std::uint32_t> knownBots;
    for (auto const& pair : m_bots)
    {
        auto const account = accounts.find(pair.second.entry->accountId);
        if (account != accounts.end())
            ++knownBots[account->second.name];
    }
    Bots::AccountChoice choice = Bots::ChooseBotAccount(m_settings.accountPrefix, pool, knownBots);
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
    record.entry = NewEntry(guid, accountId, name, role, m_settings);
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

void BotMgr::RunGroupCommand(GroupCommandMessage const& message)
{
    Player* player = ObjectAccessor::FindPlayer(message.player);
    if (!player)
        return;
    BotRecord* record = FindByName(message.botName);
    if (record && message.kind == Bots::GroupCommandKind::Invite && !BotPlayer(*record))
    {
        // An offline bot can be in a group that only bots kept, after the
        // player left it: that group does not hold the bot back.
        ObjectGuid const botGuid(HIGHGUID_PLAYER, uint32(record->entry->playerGUID));
        Group* botGroup = sObjectMgr.GetGroupByMember(botGuid);
        if (botGroup && !HasPlayerMember(*botGroup))
            Player::RemoveFromGroup(botGroup, botGuid);
    }
    if (!RunGroupPlan(message.kind, record, player) || message.kind != Bots::GroupCommandKind::Invite || BotPlayer(*record))
        return;

    // The bot is not in the world: it joins when it is.
    if (record->entry->state == PB_STATE_OFFLINE)
    {
        std::string error;
        if (!LoginBot(record->entry->name, error))
        {
            TellPlayer(player, error.c_str());
            return;
        }
    }
    record->pendingInvite = message.player;
}

bool BotMgr::RunGroupPlan(Bots::GroupCommandKind kind, BotRecord* record, Player* player)
{
    Bots::GroupCommandPlan const plan = Bots::PlanGroupCommand(kind, GroupFactsFor(record, player));
    if (!plan.ok)
    {
        TellPlayer(player, plan.line);
        return false;
    }
    // The login is the caller's step: the rest waits for the bot to be in
    // the world.
    Player* bot = BotPlayer(*record);
    if (plan.login || ((plan.addToGroup || plan.teleport) && !bot))
        return true;

    ObjectGuid const botGuid(HIGHGUID_PLAYER, uint32(record->entry->playerGUID));
    Group* group = player->GetGroup();
    if (plan.addToGroup)
    {
        if (!group)
        {
            group = new Group;
            if (!group->Create(player->GetObjectGuid(), player->GetName()))
            {
                delete group;
                return false;
            }
            sObjectMgr.AddGroup(group);
        }
        if (!group->AddMember(botGuid, bot->GetName()))
            return false;
        group->BroadcastGroupUpdate();
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %s joins the group of %s", bot->GetName(), player->GetName());
    }
    if (plan.teleport)
        bot->TeleportTo(player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());
    if (plan.leaveGroup)
    {
        Player::RemoveFromGroup(group, botGuid);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %s leaves the group of %s", record->entry->name.c_str(), player->GetName());
    }
    if (plan.logout)
    {
        record->wasInGroup = false;
        record->entry->state = PB_STATE_OFFLINE;
    }
    return true;
}

void BotMgr::UpdateGroupOf(BotRecord& record)
{
    Player* bot = BotPlayer(record);
    if (!bot)
        return; // Not in the world yet, or in a far teleport.

    if (!record.pendingInvite.IsEmpty())
    {
        ObjectGuid const inviter = record.pendingInvite;
        record.pendingInvite.Clear();
        Player* player = ObjectAccessor::FindPlayer(inviter);
        // The bot logged in only for this invite.
        if (!player || !RunGroupPlan(Bots::GroupCommandKind::Invite, &record, player))
        {
            record.entry->state = PB_STATE_OFFLINE;
            return;
        }
    }

    Group* group = bot->GetGroup();
    if (group && !HasPlayerMember(*group))
    {
        // The player left: the bots do not keep a group of their own, so
        // that a later invite finds the bot free.
        Player::RemoveFromGroup(group, bot->GetObjectGuid());
        group = nullptr;
    }
    if (record.wasInGroup && !group)
    {
        // The player removed the bot, or the group ended.
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %s is out of its group and logs out", bot->GetName());
        record.entry->state = PB_STATE_OFFLINE;
    }
    record.wasInGroup = group != nullptr;
}

bool BotMgr::HasPlayerMember(Group const& group) const
{
    // The test driver of the scripted runs sits in the player's seat.
    for (Group::MemberSlot const& slot : group.GetMemberSlots())
        if (m_bots.find(slot.guid.GetCounter()) == m_bots.end() || IsTestDriver(slot.guid))
            return true;
    return false;
}

void BotMgr::SendLines()
{
    for (BotLine const& line : m_lines.Drain())
    {
        Player* bot = ObjectAccessor::FindPlayer(line.bot);
        if (!bot || !bot->IsInWorld())
            continue;

        WorldPacket data;
        if (line.chatType == CHAT_MSG_PARTY)
        {
            Group* group = bot->GetGroup();
            if (!group)
                continue;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_PARTY, line.text.c_str(), LANG_UNIVERSAL, bot->GetChatTag(), bot->GetObjectGuid(), bot->GetName());
            group->BroadcastPacket(&data, false, group->GetMemberGroup(bot->GetObjectGuid()));
            m_run.NoteChatLine(line, bot);
        }
        else if (line.chatType == CHAT_MSG_WHISPER)
        {
            Player* receiver = ObjectAccessor::FindPlayer(line.receiver);
            if (!receiver)
                continue;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, line.text.c_str(), LANG_UNIVERSAL, bot->GetChatTag(), bot->GetObjectGuid(), bot->GetName());
            receiver->GetSession()->SendPacket(&data);
            m_run.NoteChatLine(line, bot);
        }
    }
}

void BotMgr::StartLogin(BotRecord& record, WorldSession* session)
{
    // A new AI each time, with an empty inbox and a fresh think timer.
    record.entry->ai.reset(new BotAI(m_settings.think, m_settings.startDelay, RoleOf(record.role)));
    record.entry->ai->botEntry = record.entry.get();
    record.entry->state = PB_STATE_LOADING;
    record.wasInGroup = false;
    session->SetBot(record.entry);
    sWorld.AddSession(session);
}

void BotMgr::PutOnPendingGear(BotRecord& record)
{
    // By GUID: the session of the account can hold another character.
    Player* player = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, uint32(record.entry->playerGUID)));
    if (!player || !player->IsInWorld())
        return;
    // A dead bot keeps the gear for later: the core applies item stats only
    // at the equip of a living player (see EquipCharacter).
    if (!player->IsAlive())
        return;
    if (Bots::GearTemplate const* gear = m_gear.Find(record.pendingGear))
        EquipFromGearTemplate(player, *gear);
    record.pendingGear.clear();
    // Then the level routine, for the weapon skills of the new gear.
    ApplyLevelRoutine(player);
}

bool BotMgr::EquipCharacter(std::string const& name, std::string const& templateName, std::string& report, std::string& error,
    EquipReport* equipped)
{
    std::string wanted = name;
    Player* player = normalizePlayerName(wanted) ? sObjectMgr.GetPlayer(wanted.c_str()) : nullptr;
    if (!player || !player->IsInWorld())
    {
        error = "No character named " + name + " is in the world.";
        return false;
    }
    // The core applies the stats of an item only when a living player
    // equips it, but takes them off at any removal: gear put on a dead
    // character would lower its stats for good.
    if (!player->IsAlive())
    {
        error = wanted + " is dead. Revive it first.";
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
        ApplyLevelRoutine(player);
    if (equipped)
        *equipped = {gear->name, draw};
    report = wanted + " wears " + gear->name + (draw.shape.empty() ? "" : " (shape " + draw.shape + ")") + ": " +
        (draw.picks.empty() ? std::string("nothing") : DescribeGearDraw(draw));
    return true;
}
