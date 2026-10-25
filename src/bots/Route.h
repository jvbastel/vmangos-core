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
//
// A generated route (`.route generate`) also has these lines:
//
//   seed 7                            # the seed that made the file
//   quest 5723                        # a quest of the dungeon
//   kill 11318 8                      # kill 8 of the entry
//   item 14395 1 11322 11324          # 1 item that the entries drop
//   visit q5722_maur_grimtotem        # walk to that point
//   point p4 1.00 2.00 3.00 2 quest 5723
//   pack pack9 4.00 5.00 6.00 quest 5723
//
// - `seed <number>` comes once, and only in a generated file.
// - `quest <id>` adds a quest to the quest list. The `kill`, `item`, and
//   `visit` lines below it are its objectives. A `visit` names a point of the
//   route, before or after it in the file.
// - `quest <id>` at the end of a point or pack line marks a step that is
//   only there for that quest. A run without quests skips these steps.
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
        // The quest that this point is for, or 0.
        std::uint32_t quest = 0;
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
        // The quest that this pack is for, or 0.
        std::uint32_t quest = 0;
    };

    enum class QuestObjectiveKind
    {
        Visit, // Walk to `point`.
        Kill,  // Kill `count` of `entry`.
        Item,  // Loot `count` of `item`, which `entries` drop.
    };

    struct QuestObjective
    {
        QuestObjectiveKind kind;
        std::string point;
        std::uint32_t entry = 0;
        std::uint32_t count = 0;
        std::uint32_t item = 0;
        std::vector<std::uint32_t> entries;
    };

    struct RouteQuest
    {
        std::uint32_t id;
        std::vector<QuestObjective> objectives;
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
        // Only a generated route has a seed.
        bool hasSeed = false;
        std::uint32_t seed = 0;
        std::vector<std::uint32_t> bosses;
        std::vector<RouteQuest> quests;
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

    // The names for the comments of a route file. Each one can be empty or
    // return an empty name.
    struct RouteNames
    {
        std::function<std::string(std::uint32_t entry)> entry;
        std::function<std::string(std::uint32_t quest)> quest;
        std::function<std::string(std::uint32_t item)> item;
    };
    std::string WriteRoute(Route const& route, RouteNames const& names);

    // A route name is also a file name: lower case letters, digits, and `_`.
    bool IsValidRouteName(std::string const& name);

    // The point or pack of that name, or nullptr.
    RoutePoint const* FindPoint(Route const& route, std::string const& name);
    RoutePack const* FindPack(Route const& route, std::string const& name);
    RouteQuest const* FindQuest(Route const& route, std::uint32_t id);

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
    // One more mob for the pack of that name, to fix a generated pack.
    std::string AddMobTo(Route& route, std::string const& packName, RouteMob const& mob);
    // Takes the mob of that spawn guid out of its pack.
    std::string RemoveMob(Route& route, std::uint32_t spawnGuid);
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
