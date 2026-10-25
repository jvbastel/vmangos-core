#ifndef WOWAI_BOTS_GHOST_WALK_H
#define WOWAI_BOTS_GHOST_WALK_H

#include "Route.h"

#include <cstdint>
#include <string>
#include <vector>

// The ghost run of a released bot (jvbastel/WOWAI#72): from the graveyard to
// the portal of the dungeon of its corpse, along a ghost route. A ghost
// route is a route file with only points, on the map of the graveyard. The
// first point is at the graveyard, the last one at the portal.
namespace Bots
{
    // A ghost is at a point of the route when it is this near to it.
    float const GhostPointYards = 3.0f;
    // The radius of a point of a ghost route.
    float const GhostPointRadiusYards = 2.0f;

    // The name of the ghost route to the dungeon with map id `dungeonMap`,
    // for example ghost_389 for Ragefire Chasm.
    std::string GhostRouteName(std::uint32_t dungeonMap);
    // True for the name of a ghost route, with the map of its dungeon.
    bool IsGhostRouteName(std::string const& name, std::uint32_t& dungeonMap);

    // The ghost route to the dungeon `dungeonMap` along a navmesh path on
    // map `mapId`, from the graveyard (the first spot) to the portal (the
    // last spot): a point at each turn of the path, and on a straight part
    // at most `spacing` yards after the last point. The last point is
    // named portal.
    Route MakeGhostRoute(std::uint32_t dungeonMap, std::uint32_t mapId, std::vector<Position> const& path, float spacing);

    struct GhostStep
    {
        // The ghost is at the last point: it enters the portal.
        bool atPortal;
        // Where the ghost walks next, when not at the portal.
        Position next;
    };

    // The next step of a ghost at `at` on `route`. At a point, the ghost
    // goes to the next one. Else it goes to the nearest point, or to the
    // point after it when the ghost is already past the nearest one. The
    // first place of each point counts. `route` has at least one point.
    GhostStep NextGhostStep(Route const& route, Position const& at);
}

#endif
