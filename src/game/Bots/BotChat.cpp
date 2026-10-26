/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "BotChat.h"
#include "BotMgr.h"
#include "Config/Config.h"
#include "Group.h"
#include "ItemPrototype.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"

#include <algorithm>
#include <chrono>

namespace
{
    // How often the bot manager looks for the ends of chat sessions.
    int64 const SessionCheckMs = 1000;

    int64 NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void LogChat(std::string const& text)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[BotChat] %s", text.c_str());
    }

    char const* RaceWord(uint8 race)
    {
        switch (race)
        {
            case RACE_HUMAN: return "human";
            case RACE_ORC: return "orc";
            case RACE_DWARF: return "dwarf";
            case RACE_NIGHTELF: return "night elf";
            case RACE_UNDEAD: return "undead";
            case RACE_TAUREN: return "tauren";
            case RACE_GNOME: return "gnome";
            case RACE_TROLL: return "troll";
            default: return "unknown";
        }
    }

    char const* ClassWord(uint8 playerClass)
    {
        switch (playerClass)
        {
            case CLASS_WARRIOR: return "warrior";
            case CLASS_PALADIN: return "paladin";
            case CLASS_HUNTER: return "hunter";
            case CLASS_ROGUE: return "rogue";
            case CLASS_PRIEST: return "priest";
            case CLASS_SHAMAN: return "shaman";
            case CLASS_MAGE: return "mage";
            case CLASS_WARLOCK: return "warlock";
            case CLASS_DRUID: return "druid";
            default: return "unknown";
        }
    }

    // The contract has three roles: melee and ranged are both dps.
    std::string ChatRole(std::string const& role)
    {
        return role == "tank" || role == "healer" ? role : "dps";
    }

    Player* OnlinePlayer(ObjectGuid guid)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
        return player && player->IsInWorld() ? player : nullptr;
    }

    // Our bot, online, as the chat contract sees it.
    bool ChatBotOf(Player* player, Bots::ChatEventBot& bot)
    {
        std::string role;
        if (!player || !player->IsInWorld() || !sBotMgr.RoleOfOurBot(player, role))
            return false;
        bot.guid = player->GetObjectGuid().GetRawValue();
        bot.name = player->GetName();
        bot.race = RaceWord(player->GetRace());
        bot.botClass = ClassWord(player->GetClass());
        bot.role = ChatRole(role);
        return true;
    }

    std::string ZoneName(Player* player)
    {
        if (!player)
            return "";
        AreaEntry const* zone = AreaEntry::GetById(player->GetZoneId());
        return zone && zone->Name ? zone->Name : "";
    }

    // The group as bot chat sees it. The player is the leader, or else the
    // first member that is no bot (of ours or of PlayerBotMgr). The test driver
    // of the scripted runs counts as a player.
    Bots::ChatGroupView ViewOf(Group* group)
    {
        Bots::ChatGroupView view;
        view.groupId = group->GetId();

        std::vector<Group::MemberSlot const*> players;
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            Player* member = OnlinePlayer(slot.guid);
            Bots::ChatEventBot bot;
            if (ChatBotOf(member, bot))
                view.bots.push_back(bot);
            else if (sBotMgr.IsTestDriver(slot.guid) || (!sBotMgr.IsBotCharacter(slot.guid) && !(member && member->IsBot())))
                players.push_back(&slot);
        }
        auto leader = std::find_if(players.begin(), players.end(),
            [group](Group::MemberSlot const* slot) { return slot->guid == group->GetLeaderGuid(); });
        Group::MemberSlot const* player = leader != players.end() ? *leader : (players.empty() ? nullptr : players.front());
        if (player)
        {
            view.player = {player->guid.GetRawValue(), player->name};
            Player* online = OnlinePlayer(player->guid);
            view.playerOnline = online != nullptr;
            view.zone = ZoneName(online);
        }
        return view;
    }
}

void BotChat::LoadConfig()
{
    Bots::ChatClientSettings settings;
    settings.enable = sConfig.GetBoolDefault("BotChat.Enable", true);
    settings.url = sConfig.GetStringDefault("BotChat.Url", "http://botchat:8000");
    settings.workers = std::max(1, sConfig.GetIntDefault("BotChat.Workers", 4));
    settings.queueSize = std::max(1, sConfig.GetIntDefault("BotChat.QueueSize", 64));
    settings.timeoutMs = std::max(100, sConfig.GetIntDefault("BotChat.TimeoutMs", 5000));

    if (m_client && (settings.enable != m_settings.enable || settings.url != m_settings.url ||
            settings.workers != m_settings.workers || settings.queueSize != m_settings.queueSize ||
            settings.timeoutMs != m_settings.timeoutMs))
    {
        // The workers keep their connections: a change needs a restart.
        LogChat("the BotChat.* settings take effect at the next start");
        return;
    }
    m_settings = settings;
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[BotChat] Enable %u, %s, %u workers, queue %u, timeout %u ms, contract version %d",
        uint32(m_settings.enable), m_settings.url.c_str(), m_settings.workers, uint32(m_settings.queueSize),
        m_settings.timeoutMs, Bots::ChatContractVersion);
}

void BotChat::Start()
{
    if (m_client || !m_settings.enable)
        return;
    m_client.reset(new Bots::ChatClient(m_settings, NowMs, LogChat));
}

std::vector<BotLine> BotChat::Update()
{
    std::vector<BotLine> lines;
    if (!m_client)
    {
        m_messages.Drain();
        return lines;
    }

    for (PlayerChatMessage const& message : m_messages.Drain())
        PostPlayerMessage(message);

    int64 const now = NowMs();
    if (now - m_lastSessionCheck >= SessionCheckMs)
    {
        m_lastSessionCheck = now;
        UpdateSessions();
    }

    for (Bots::DueChatLine const& due : m_client->TakeDue(now))
    {
        BotLine line;
        if (!MaySay(due, line))
            continue;
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[BotChat] %s says (group %u): %s", OnlinePlayer(line.bot)->GetName(),
            due.groupId, line.text.c_str());
        lines.push_back(line);
    }
    return lines;
}

void BotChat::PostPlayerMessage(PlayerChatMessage const& message)
{
    Player* sender = OnlinePlayer(message.sender);
    if (!sender)
        return;
    Group* group = sender->GetGroup();

    Bots::ChatGroupView view;
    Bots::ChatMessage chat;
    chat.text = message.text;
    if (message.chatType == CHAT_MSG_WHISPER)
    {
        chat.channel = Bots::ChatChannel::Whisper;
        chat.toBotGuid = message.receiver.GetRawValue();
        Player* bot = OnlinePlayer(message.receiver);
        if (group && bot && bot->GetGroup() == group)
            view = ViewOf(group);
        else
        {
            // A bot out of the group of the player: a talk of two, with no
            // group (ID 0).
            Bots::ChatEventBot chatBot;
            if (!ChatBotOf(bot, chatBot))
                return;
            view.bots.push_back(chatBot);
        }
    }
    else
    {
        if (!group)
            return;
        view = ViewOf(group);
    }
    if (view.bots.empty())
        return;
    // The sender is the player of the event.
    view.player = {sender->GetObjectGuid().GetRawValue(), sender->GetName()};
    view.zone = ZoneName(sender);

    Bots::ChatEvent event = Bots::MakeChatEvent(view, Bots::ChatEventKind::PlayerMessage);
    event.message = chat;
    Post(event, message.chatType == CHAT_MSG_WHISPER ? message.receiver : ObjectGuid());
}

void BotChat::UpdateSessions()
{
    // The groups of our online bots, and the groups of the open sessions.
    std::vector<Group*> groups;
    auto const add = [&groups](Group* group) {
        if (group && std::find(groups.begin(), groups.end(), group) == groups.end())
            groups.push_back(group);
    };
    for (ObjectGuid const& guid : sBotMgr.OnlineBots())
        if (Player* bot = OnlinePlayer(guid))
            add(bot->GetGroup());
    for (uint32 id : m_sessions.Groups())
        add(sObjectMgr.GetGroupById(id));

    std::vector<Bots::ChatGroupView> views;
    for (Group* group : groups)
        views.push_back(ViewOf(group));

    for (Bots::ChatEvent const& end : m_sessions.Update(views))
    {
        static char const* const reasons[] = {"bot_left", "group_disbanded", "player_logout"};
        std::string bots;
        for (Bots::Guid guid : end.endedBotGuids)
            bots += (bots.empty() ? "" : ", ") + std::to_string(guid);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[BotChat] session_end of group %u (%s): bots %s",
            end.groupId, reasons[int(end.reason)], bots.c_str());
        Post(end);
    }
}

void BotChat::PostGroupEvent(uint32 groupId, Bots::GroupEvent const& event)
{
    if (!m_client)
        return;
    Group* group = sObjectMgr.GetGroupById(groupId);
    if (!group)
        return;

    // The fact that only the game knows: the boss name, or the item name.
    std::string name;
    if (event.kind == Bots::GroupEventKind::BossKill)
    {
        if (CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(event.entry))
            name = info->name;
    }
    else if (event.kind == Bots::GroupEventKind::LootRoll)
    {
        if (ItemPrototype const* proto = sObjectMgr.GetItemPrototype(event.entry))
            name = proto->Name1;
    }

    Bots::ChatEvent chatEvent;
    if (!Bots::MakeGroupChatEvent(ViewOf(group), event, name, chatEvent))
        return;
    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[BotChat] post %s", Bots::EncodeChatEvent(chatEvent).c_str());
    Post(chatEvent);
}

void BotChat::Post(Bots::ChatEvent const& event, ObjectGuid toBot)
{
    uint64 const id = m_client->Post(event);
    m_sent.push_back({id, event.groupId, event.kind, toBot});
}

std::vector<SentChatEvent> BotChat::TakeSent()
{
    std::vector<SentChatEvent> sent;
    sent.swap(m_sent);
    return sent;
}

std::vector<Bots::ChatEventOutcome> BotChat::TakeOutcomes()
{
    return m_client ? m_client->TakeOutcomes() : std::vector<Bots::ChatEventOutcome>();
}

bool BotChat::MaySay(Bots::DueChatLine const& due, BotLine& line) const
{
    Player* bot = OnlinePlayer(ObjectGuid(due.line.botGuid));
    Bots::ChatEventBot chatBot;
    if (!ChatBotOf(bot, chatBot))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[BotChat] dropped a line of bot %u: it logged out", uint32(due.line.botGuid));
        return false;
    }
    if (due.groupId != 0 && (!bot->GetGroup() || bot->GetGroup()->GetId() != due.groupId))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[BotChat] dropped a line of %s: it left the group %u", bot->GetName(), due.groupId);
        return false;
    }

    line.bot = bot->GetObjectGuid();
    line.text = due.line.text;
    line.eventId = due.eventId;
    if (due.line.channel == Bots::ChatChannel::Whisper)
    {
        line.chatType = CHAT_MSG_WHISPER;
        line.receiver = ObjectGuid(due.playerGuid);
    }
    else
    {
        if (due.groupId == 0)
            return false;
        line.chatType = CHAT_MSG_PARTY;
    }
    return true;
}
