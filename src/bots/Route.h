#ifndef WOWAI_BOTS_ROUTE_H
#define WOWAI_BOTS_ROUTE_H

#include "BotTypes.h"

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// The route of a dungeon: an ordered list of points and packs, and the boss
// list of the dungeon. The script runner, the GM route recorder, and the
// later automatic pull all use this model. A route file looks like this:
//
//   route ragefire_chasm 389          # name and map id
//   boss 11517                        # Oggleflint
//   point entrance 3.81 -14.82 -17.84 2
//   alt 5.10 -12.00 -17.80 2          # another choice for the point above
//   pack pack1 -3.20 10.10 -20.00     # where the first mob stood
//   mob 79123 11319                   # spawn guid and entry: Ragefire Shaman
//   mob 79124 11321
//
// - `route <name> <map>` comes first, once.
// - `boss <entry>` adds a creature entry to the boss list.
// - `point <name> <x> <y> <z> <radius>` is a step: a bot walks to a random
//   spot inside the circle.
// - `alt <x> <y> <z> <radius>` is one more place for the point above it. The
//   bot picks one of the places of the point.
// - `pack <name> <x> <y> <z>` is a step: a group of enemies to fight there.
// - `mob <spawn guid> <entry>` adds a creature to the pack above it. The
//   spawn guid is the `guid` of the `creature` table.
// Points and packs share one set of names, so a script can name either.
namespace Bots
{
    // A circle that a bot walks into.
    struct RoutePlace
    {
        Position position;
        float radius;
    };

    struct RoutePoint
    {
        std::string name;
        // The first place, then the alternative places. Never empty.
        std::vector<RoutePlace> places;
    };

    struct RouteMob
    {
        std::uint32_t spawnGuid;
        std::uint32_t entry;
    };

    struct RoutePack
    {
        std::string name;
        Position position;
        std::vector<RouteMob> mobs;
    };

    enum class RouteStepKind
    {
        Point,
        Pack,
    };

    // One step of the route, in file order. `index` is into `points` or
    // `packs` of the route, by kind.
    struct RouteStep
    {
        RouteStepKind kind;
        std::size_t index;
    };

    struct Route
    {
        std::string name;
        std::uint32_t mapId = 0;
        std::vector<std::uint32_t> bosses;
        std::vector<RoutePoint> points;
        std::vector<RoutePack> packs;
        std::vector<RouteStep> steps;
    };

    struct RouteResult
    {
        bool ok;
        Route route;       // Only when ok.
        std::string error; // "line <n>: ..." when not ok.
    };

    // Reads a route file. It uses the shared tokenizer, so `#` starts a
    // comment.
    RouteResult ParseRoute(std::string const& text);

    // The text of a route file that ParseRoute reads back to the same route.
    // `entryName` gives the creature name of an entry for the comment of boss
    // and mob lines. It can return an empty name.
    std::string WriteRoute(Route const& route,
        std::function<std::string(std::uint32_t entry)> const& entryName);

    // A route name is also a file name: lower case letters, digits, and `_`.
    bool IsValidRouteName(std::string const& name);

    // The point or pack of that name, or nullptr.
    RoutePoint const* FindPoint(Route const& route, std::string const& name);
    RoutePack const* FindPack(Route const& route, std::string const& name);

    // The edits of the GM route recorder. Each one returns an empty string
    // when it worked, or a short line for the GM.
    // With an empty name, AddPoint names the point p1, p2, ..., and AddPack
    // names the pack pack1, pack2, ... (the first free number).
    std::string AddPoint(Route& route, std::string name, RoutePlace const& place);
    // One more place for the last point of the route.
    std::string AddAlternative(Route& route, RoutePlace const& place);
    std::string AddPack(Route& route, std::string name, Position const& position,
        std::vector<RouteMob> const& mobs);
    // One more mob for the last pack of the route.
    std::string AddMob(Route& route, RouteMob const& mob);
    std::string AddBoss(Route& route, std::uint32_t entry);
    // Removes the last step, with its alternatives or mobs.
    std::string RemoveLastStep(Route& route);

    // The boss lists of the route files, by dungeon map. A kill of one of
    // these bosses is a `boss_kill` for bot chat.
    class BossList
    {
    public:
        void Add(Route const& route);
        bool IsBoss(std::uint32_t mapId, std::uint32_t entry) const;
        bool Empty() const { return m_bosses.empty(); }

    private:
        std::set<std::pair<std::uint32_t, std::uint32_t>> m_bosses; // (map, entry)
    };
}

#endif
