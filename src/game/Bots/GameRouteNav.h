/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_GAME_ROUTE_NAV_H
#define WOWAI_GAME_GAME_ROUTE_NAV_H

#include "RouteGenerator.h"

#include "Detour/Include/DetourNavMeshQuery.h"

#include <vector>

class Player;

// The navmesh of the map of a walker, for the route generator and for the
// safe cast place of a bot (BotAdapter::FindGroundPath). Paths come from
// PathInfo with the walker, so they leave out lava like the path of a
// walking player.
//
// Threads: on the thread of the walker's map. The navmesh query is per thread.
class GameRouteNav : public Bots::RouteNav
{
    public:
        explicit GameRouteNav(Player* walker);

        bool Ready() const { return m_query != nullptr; }
        // Paths of corners only: a smooth path of PathInfo has at most 256
        // steps of 4 yards, so a walk of more than about 1000 yards fails.
        void SetStraightPaths(bool straight) { m_straight = straight; }

        // A walk on the ground, with no step on lava or slime.
        bool Path(Bots::Position const& from, Bots::Position const& to, std::vector<Bots::Position>& out) override;
        Bots::Position Reach(Bots::Position const& from, float dx, float dy, float distance) override;
        bool OnMesh(Bots::Position const& position) override;
        // The position lies on lava or slime, and on no ground.
        bool OnLava(Bots::Position const& position);

    private:
        // True when the position lies on a polygon that the filter takes.
        bool Inside(Bots::Position const& position, dtQueryFilter const& filter);

        Player* m_walker;
        dtNavMeshQuery const* m_query = nullptr;
        dtQueryFilter m_ground;
        dtQueryFilter m_lava;
        bool m_straight = false;
};

#endif
