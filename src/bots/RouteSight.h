#ifndef WOWAI_BOTS_ROUTE_SIGHT_H
#define WOWAI_BOTS_ROUTE_SIGHT_H

#include "BotTypes.h"
#include "RouteGenerator.h"
#include "RouteLine.h"

#include <vector>

// What a group at a spot sees of a pack, with the navmesh: the rules that
// the route generator (RouteGenerator.h) and the hand pack order
// (RouteHandOrder.h) share.
namespace Bots
{
    // The member of the pack whose spawn point is nearest to `at`.
    Position NearestMember(Position const& at, GeneratorPack const& pack, std::vector<GeneratorSpawn> const& spawns);

    enum class Sight
    {
        OutOfRange,
        Hidden,     // In range, but no line of sight.
        OtherLevel, // In range and in sight, but far by path.
        Pullable,
    };

    struct PackSight
    {
        Sight sight;
        float path; // The walk to the nearest member, or -1 with no path.
    };

    // Whether a group at `at` can pull the pack: the pack is within
    // `range`, sees the spot, and stands on its level. On the same level,
    // the navmesh path to the pack is not much longer than the straight
    // distance. With no path at all, for example from a spawn just off
    // the navmesh, the height tells the level.
    PackSight SightOf(Position const& at, GeneratorPack const& pack, std::vector<GeneratorSpawn> const& spawns,
        GeneratorSettings const& settings, RouteNav& nav, float range);

    // The pull point on a path to a pack: the first spot whose rest of the
    // path is within the limit and that is within aggro range plus the
    // margin of the pack. Without one, the first spot within the limit.
    float PullOnPath(RouteLine const& way, GeneratorPack const& pack, std::vector<GeneratorSpawn> const& spawns,
        GeneratorSettings const& settings);

    // The spot in the pull band of the pack, out from its leader in the
    // direction `angle`: out of the aggro range of the pack, with its
    // wander and its other members, and within aggro range plus margin.
    // False when a wall stops the spot before the band.
    bool PullBandSpot(Position const& leader, float angle, GeneratorPack const& pack,
        std::vector<GeneratorSpawn> const& spawns, GeneratorSettings const& settings, RouteNav& nav, Position& spot);

    // True when a group at `place`, step `step` of the route, pulls a pack
    // other than `own` that the route fights after that step, or never
    // (foughtAt holds the step of each pack's fight, or a later number). A
    // pack that cannot see the place, or is on another level, is no danger.
    bool PullsPackOfLater(Position const& place, std::vector<std::size_t> const& foughtAt, std::size_t step,
        std::size_t own, std::vector<GeneratorPack> const& packs, std::vector<GeneratorSpawn> const& spawns,
        GeneratorSettings const& settings, RouteNav& nav);
}

#endif
