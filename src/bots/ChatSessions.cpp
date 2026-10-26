#include "ChatSessions.h"

#include <algorithm>

namespace
{
    bool HasBot(std::vector<Bots::ChatEventBot> const& bots, Bots::Guid guid)
    {
        return std::any_of(bots.begin(), bots.end(), [guid](Bots::ChatEventBot const& bot) { return bot.guid == guid; });
    }

    Bots::ChatEvent SessionEnd(Bots::ChatGroupView const& session, std::vector<Bots::Guid> ended, Bots::SessionEndReason reason)
    {
        Bots::ChatEvent event = Bots::MakeChatEvent(session, Bots::ChatEventKind::SessionEnd);
        event.endedBotGuids = std::move(ended);
        event.reason = reason;
        return event;
    }

    std::vector<Bots::Guid> AllBots(Bots::ChatGroupView const& session)
    {
        std::vector<Bots::Guid> guids;
        for (Bots::ChatEventBot const& bot : session.bots)
            guids.push_back(bot.guid);
        return guids;
    }
}

Bots::ChatEvent Bots::MakeChatEvent(ChatGroupView const& group, ChatEventKind kind)
{
    ChatEvent event;
    event.kind = kind;
    event.groupId = group.groupId;
    event.player = group.player;
    event.bots = group.bots;
    event.zone = group.zone;
    return event;
}

std::vector<Bots::ChatEvent> Bots::ChatSessions::Update(std::vector<ChatGroupView> const& groups)
{
    std::map<std::uint32_t, ChatGroupView const*> now;
    for (ChatGroupView const& group : groups)
        now[group.groupId] = &group;

    std::vector<ChatEvent> events;
    std::map<std::uint32_t, ChatGroupView> sessions;
    for (auto const& pair : m_sessions)
    {
        ChatGroupView const& session = pair.second;
        auto const found = now.find(pair.first);
        if (found == now.end() || found->second->player.guid != session.player.guid)
        {
            // The group is gone, or the player is no longer in it.
            events.push_back(SessionEnd(session, AllBots(session), SessionEndReason::GroupDisbanded));
            continue;
        }
        ChatGroupView const& group = *found->second;
        if (!group.playerOnline)
        {
            events.push_back(SessionEnd(session, AllBots(session), SessionEndReason::PlayerLogout));
            continue;
        }
        std::vector<Guid> left;
        for (ChatEventBot const& bot : session.bots)
            if (!HasBot(group.bots, bot.guid))
                left.push_back(bot.guid);
        if (!left.empty())
            events.push_back(SessionEnd(session, left, SessionEndReason::BotLeft));
    }

    // The sessions that go on or start: the player is online with bots.
    for (ChatGroupView const& group : groups)
        if (group.player.guid != 0 && group.playerOnline && !group.bots.empty())
            sessions[group.groupId] = group;
    m_sessions.swap(sessions);
    return events;
}

std::vector<std::uint32_t> Bots::ChatSessions::Groups() const
{
    std::vector<std::uint32_t> ids;
    for (auto const& pair : m_sessions)
        ids.push_back(pair.first);
    return ids;
}

bool Bots::MakeGroupChatEvent(ChatGroupView const& group, GroupEvent const& event, std::string const& name, ChatEvent& chatEvent)
{
    if (group.player.guid == 0 || !group.playerOnline || group.bots.empty())
        return false;
    switch (event.kind)
    {
        case GroupEventKind::BossKill:
            chatEvent = MakeChatEvent(group, ChatEventKind::BossKill);
            chatEvent.bossName = name;
            return true;
        case GroupEventKind::Wipe:
            chatEvent = MakeChatEvent(group, ChatEventKind::Wipe);
            return true;
        case GroupEventKind::LootRoll:
            chatEvent = MakeChatEvent(group, ChatEventKind::LootRoll);
            chatEvent.itemName = name;
            chatEvent.winnerGuid = event.winner;
            return true;
        case GroupEventKind::Rest:
            if (!HasBot(group.bots, event.bot))
                return false;
            chatEvent = MakeChatEvent(group, ChatEventKind::Rest);
            chatEvent.restingBotGuid = event.bot;
            return true;
    }
    return false;
}
