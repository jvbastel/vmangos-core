#include "GroupCoordinator.h"

namespace Bots
{
    using Lock = std::lock_guard<std::mutex>;

    Guid GroupCoordinator::GetFocusTarget() const
    {
        Lock lock(m_mutex);
        return m_focusTarget;
    }

    void GroupCoordinator::SetFocusTarget(Guid target)
    {
        Lock lock(m_mutex);
        m_focusTarget = target;
    }

    PullState GroupCoordinator::GetPull() const
    {
        Lock lock(m_mutex);
        return m_pull;
    }

    void GroupCoordinator::StartPull(Guid puller, Guid target)
    {
        Lock lock(m_mutex);
        m_pull = {PullPhase::Pulling, puller, target};
        m_focusTarget = target;
    }

    void GroupCoordinator::HoldPull(Guid puller)
    {
        Lock lock(m_mutex);
        if (m_pull.phase == PullPhase::Pulling && m_pull.puller == puller)
            m_pull.phase = PullPhase::Holding;
    }

    void GroupCoordinator::EndPull()
    {
        Lock lock(m_mutex);
        m_pull = {PullPhase::None, NoGuid, NoGuid};
        m_focusTarget = NoGuid;
    }

    bool GroupCoordinator::TankHoldsThreat() const
    {
        Lock lock(m_mutex);
        if (m_pull.phase == PullPhase::None)
            return m_focusTarget != NoGuid;
        return m_pull.phase == PullPhase::Holding;
    }

    BossFight GroupCoordinator::GetBossFight() const
    {
        Lock lock(m_mutex);
        return m_bossFight;
    }

    bool GroupCoordinator::StartBossFight(Guid boss, std::uint32_t entry)
    {
        Lock lock(m_mutex);
        if (m_bossFight.boss != NoGuid)
            return m_bossFight.boss == boss;
        m_bossFight = {boss, entry};
        return true;
    }

    void GroupCoordinator::EndBossFight(Guid boss)
    {
        Lock lock(m_mutex);
        if (m_bossFight.boss == boss)
            m_bossFight = {NoGuid, 0};
    }

    bool GroupCoordinator::IsWiped() const
    {
        Lock lock(m_mutex);
        return m_wiped;
    }

    void GroupCoordinator::SetWiped(bool wiped)
    {
        Lock lock(m_mutex);
        if (wiped && !m_wiped)
            m_events.push_back({GroupEventKind::Wipe, NoGuid, RestReason::None});
        m_wiped = wiped;
    }

    void GroupCoordinator::SetResting(Guid bot, RestReason reason)
    {
        Lock lock(m_mutex);
        if (reason == RestReason::None)
        {
            m_resting.erase(bot);
            return;
        }
        if (m_resting.emplace(bot, reason).second)
            m_events.push_back({GroupEventKind::Rest, bot, reason});
    }

    std::vector<Guid> GroupCoordinator::GetResting() const
    {
        Lock lock(m_mutex);
        std::vector<Guid> bots;
        for (auto const& pair : m_resting)
            bots.push_back(pair.first);
        return bots;
    }

    void GroupCoordinator::NoteRevive(Guid bot)
    {
        Lock lock(m_mutex);
        m_events.push_back({GroupEventKind::Rest, bot, RestReason::Revive});
    }

    bool GroupCoordinator::FirstNote(std::pair<Guid, std::int64_t> key, std::int64_t nowMs)
    {
        for (auto it = m_seen.begin(); it != m_seen.end();)
        {
            if (nowMs - it->second > GroupEventMemoryMs)
                it = m_seen.erase(it);
            else
                ++it;
        }
        return m_seen.emplace(key, nowMs).second;
    }

    void GroupCoordinator::NoteBossKill(Guid boss, std::uint32_t entry, std::int64_t nowMs)
    {
        Lock lock(m_mutex);
        // Key -1: no loot slot, so a kill never matches a roll.
        if (!FirstNote({boss, -1}, nowMs))
            return;
        GroupEvent event = {GroupEventKind::BossKill};
        event.target = boss;
        event.entry = entry;
        m_events.push_back(event);
    }

    void GroupCoordinator::NoteRollWon(Guid looted, std::uint32_t itemSlot, std::uint32_t itemEntry, Guid winner, std::int64_t nowMs)
    {
        Lock lock(m_mutex);
        if (!FirstNote({looted, itemSlot}, nowMs))
            return;
        GroupEvent event = {GroupEventKind::LootRoll};
        event.target = looted;
        event.entry = itemEntry;
        event.winner = winner;
        m_events.push_back(event);
    }

    std::vector<GroupEvent> GroupCoordinator::TakeEvents()
    {
        Lock lock(m_mutex);
        std::vector<GroupEvent> events;
        events.swap(m_events);
        return events;
    }
}
