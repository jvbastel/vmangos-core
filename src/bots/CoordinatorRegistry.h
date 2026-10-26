#ifndef WOWAI_BOTS_COORDINATOR_REGISTRY_H
#define WOWAI_BOTS_COORDINATOR_REGISTRY_H

#include "CoordinatorChoice.h"
#include "GroupCoordinator.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <utility>
#include <vector>

namespace Bots
{
    // The coordinators of the groups (spec section 6). Each group has one
    // group coordinator for the bots on the map and instance of the leader.
    // The bots of the group that sit together in another place share one
    // place coordinator for that group and place, until the leader comes
    // there or no bot used it for PlaceIdleMs. Its events are no group
    // events: TakeEvents drops them.
    //
    // Threads: each call takes a small mutex. The bot manager owns it.
    class CoordinatorRegistry
    {
    public:
        // A place coordinator that no bot asked for in this time ends.
        static std::int64_t const PlaceIdleMs = 60 * 1000;

        // The coordinator of a bot of group `group` in place `bot`, with its
        // leader in place `leader` (ChooseCoordinator). `nowMs`: any steady
        // clock in milliseconds.
        std::shared_ptr<GroupCoordinator> For(std::uint32_t group, MapPlace bot, LeaderPlace leader, std::int64_t nowMs);
        // The group coordinator of group `group`.
        std::shared_ptr<GroupCoordinator> OfGroup(std::uint32_t group);
        // The next call for the group makes new coordinators.
        void Drop(std::uint32_t group);
        // Ends the coordinators of each group for which `groupExists` is
        // false, and each place coordinator that no bot asked for in
        // PlaceIdleMs.
        void DropOld(std::function<bool(std::uint32_t)> const& groupExists, std::int64_t nowMs);
        // The events of the group coordinators since the last call, with
        // their group id.
        std::vector<std::pair<std::uint32_t, GroupEvent>> TakeEvents();

    private:
        using PlaceKey = std::tuple<std::uint32_t /*group*/, std::uint32_t /*map*/, std::uint32_t /*instance*/>;
        struct PlaceCoordinator
        {
            std::shared_ptr<GroupCoordinator> coordinator;
            std::int64_t lastUseMs;
        };

        std::shared_ptr<GroupCoordinator>& GroupEntry(std::uint32_t group);

        std::mutex m_mutex;
        std::map<std::uint32_t, std::shared_ptr<GroupCoordinator>> m_groups;
        std::map<PlaceKey, PlaceCoordinator> m_places;
    };
}

#endif
