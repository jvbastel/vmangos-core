#include "CoordinatorRegistry.h"

#include <iterator>

namespace Bots
{
    std::shared_ptr<GroupCoordinator>& CoordinatorRegistry::GroupEntry(std::uint32_t group)
    {
        std::shared_ptr<GroupCoordinator>& coordinator = m_groups[group];
        if (!coordinator)
            coordinator = std::make_shared<GroupCoordinator>();
        return coordinator;
    }

    std::shared_ptr<GroupCoordinator> CoordinatorRegistry::For(std::uint32_t group, MapPlace bot, LeaderPlace leader,
        std::int64_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (ChooseCoordinator(true, bot, leader) == CoordinatorChoice::Group)
        {
            // The leader is with the bots now: the state of their place ends.
            if (leader.known)
                m_places.erase(PlaceKey{group, leader.place.map, leader.place.instance});
            return GroupEntry(group);
        }
        PlaceCoordinator& place = m_places[PlaceKey{group, bot.map, bot.instance}];
        if (!place.coordinator)
            place.coordinator = std::make_shared<GroupCoordinator>();
        place.lastUseMs = nowMs;
        return place.coordinator;
    }

    std::shared_ptr<GroupCoordinator> CoordinatorRegistry::OfGroup(std::uint32_t group)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return GroupEntry(group);
    }

    void CoordinatorRegistry::Drop(std::uint32_t group)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_groups.erase(group);
        for (auto it = m_places.begin(); it != m_places.end();)
            it = std::get<0>(it->first) == group ? m_places.erase(it) : std::next(it);
    }

    void CoordinatorRegistry::DropOld(std::function<bool(std::uint32_t)> const& groupExists, std::int64_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_groups.begin(); it != m_groups.end();)
            it = groupExists(it->first) ? std::next(it) : m_groups.erase(it);
        for (auto it = m_places.begin(); it != m_places.end();)
        {
            bool const old = !groupExists(std::get<0>(it->first)) || nowMs - it->second.lastUseMs > PlaceIdleMs;
            it = old ? m_places.erase(it) : std::next(it);
        }
    }

    std::vector<std::pair<std::uint32_t, GroupEvent>> CoordinatorRegistry::TakeEvents()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::pair<std::uint32_t, GroupEvent>> events;
        for (auto const& pair : m_groups)
            for (GroupEvent const& event : pair.second->TakeEvents())
                events.emplace_back(pair.first, event);
        // The bots away from the leader are no part of the group's events.
        for (auto const& pair : m_places)
            pair.second.coordinator->TakeEvents();
        return events;
    }
}
