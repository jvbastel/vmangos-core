#ifndef WOWAI_BOTS_ROUTE_HAND_ORDER_H
#define WOWAI_BOTS_ROUTE_HAND_ORDER_H

#include "BotTypes.h"
#include "Route.h"
#include "RouteGenerator.h"

#include <cstdint>
#include <string>
#include <vector>

// The hand pack order (WOWAI#147, #151) of the order file
// <route>.order.txt (ParsePackOrder in RouteGenerator.h), as the route
// generator and the trace build (RouteTrace.h) both apply it to their steps.
// Packs are named by the spawn guid of a mob, by custom the first mob.
//
// - `pack <guid>` lines: the packs come in this order, from where the route
//   met the first of them. A pack that the route did not fight is added:
//   the route walks from the step before to its pull point, and back.
// - `skip <guid>` lines: the pack is off the route. The builders leave it
//   out before they place the packs, so no quest takes it back, and the
//   route check warns where the route comes within its aggro range.
namespace Bots
{
    enum class RouteEventKind
    {
        Point, // A point along the line or a detour.
        Mark,  // A checkpoint of a trace.
        Pull,  // The pull point of a pack.
        Pack,
        Visit,
    };

    // A step of a route before it has its places and its name.
    struct RouteEvent
    {
        float s;   // The distance along the line.
        int order; // The order of the kinds at one s.
        int sub;   // The order inside one s and kind.
        RouteEventKind kind;
        Position center;
        float dx = 1.0f; // The flat direction of the walk there.
        float dy = 0.0f;
        std::size_t pack = 0; // For Pull and Pack: an index into the packs.
        std::uint32_t quest = 0;
        std::string name; // Of a visit.
        Position spot{};  // Of a visit: the spot to visit.
        int detour = -1;  // The detour of a step on a detour.
        // The hand order changed the walk to this step: it moved or added
        // the step, or took a step before it away.
        bool rejoin = false;
    };

    // The packs of the hand order.
    struct HandOrderPacks
    {
        std::vector<std::size_t> order; // Pack indices, in the order of the hand.
        std::vector<bool> skipped;      // Per pack.
    };

    // The packs of the guids. A guid of no pack, a pack named twice, or a
    // pack both named and skipped (it is skipped) is a warning.
    HandOrderPacks ResolveHandOrder(std::vector<std::uint32_t> const& order, std::vector<std::uint32_t> const& skips,
        std::vector<GeneratorPack> const& packs, std::vector<GeneratorSpawn> const& spawns,
        std::vector<std::string>& warnings);

    // The steps in the hand order: the pull point and the fight of each
    // named pack move, in the order of the hand, to where the route fought
    // the first of them; with no named pack on the route, after the point
    // nearest to the first added pack by path. The other steps keep their
    // order. A named pack is a pack of the route, not of a quest, so that a
    // run without the quests fights it in its turn too. A detour with no
    // fight left loses its points. A named pull point that would pull a
    // pack fought later, and the pull point of an added pack, is the spot
    // around its pack with the shortest walk from the step before.
    // Points along the navmesh path join each step whose walk changed to
    // the step before it.
    std::vector<RouteEvent> ApplyHandOrder(std::vector<RouteEvent> const& events,
        std::vector<std::size_t> const& order, std::vector<GeneratorPack> const& packs,
        std::vector<GeneratorSpawn> const& spawns, GeneratorSettings const& settings, RouteNav& nav,
        std::vector<std::string>& warnings);

    // One warning for each skipped pack that the walk between the points of
    // the route comes within `aggroRange` of: it can join a fight as an
    // extra pack.
    std::vector<std::string> SkippedPackWarnings(Route const& route, std::vector<GeneratorPack> const& packs,
        std::vector<GeneratorSpawn> const& spawns, std::vector<bool> const& skipped, float aggroRange);
}

#endif
