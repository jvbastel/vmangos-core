#ifndef WOWAI_BOTS_ROUTE_GENERATOR_H
#define WOWAI_BOTS_ROUTE_GENERATOR_H

#include "BotTypes.h"
#include "Route.h"

#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <vector>

// The route generator: it makes the route file of a dungeon from the spawns
// of the world database and the navmesh of the map. The GM command
// `.route generate` gives it the data, and it gives back the route that the
// writer of Route.h writes.
//
// How it works:
// 1. Packs: the members of each creature group, plus spawns close to each
//    other (BuildPacks).
// 2. The center line: a navmesh path from the entrance to each boss, in the
//    order of the boss list.
// 3. A pack is on the route when a group that walks the center line can
//    pull it: any member, with its wander distance or its patrol, comes
//    within aggro range plus a margin of the line, and from that spot the
//    pack has line of sight and stands on the same level (the navmesh path
//    is not much longer than the straight distance). A pack on another
//    level of a cave is thus on the route only where the line goes to that
//    level. The route fights the packs in the order the line reaches them,
//    from the first such spot whose path to the pack is short
//    (pullPathLimit). The packs in range that the line cannot pull get a
//    warning with the reason.
// 4. Route points every few yards along the line, moved sideways by a random
//    distance that stays on the navmesh. Wide rooms give alternative points.
// 5. The quest objectives: a visit point, and packs added off the line until
//    the route meets each kill count and holds a dropper of each quest item.
namespace Bots
{
    // One spawn of the `creature` table on the map.
    struct GeneratorSpawn
    {
        std::uint32_t guid;
        std::uint32_t entry;
        Position position;
        float wanderDistance = 0.0f;
        // The waypoints of a patrol, or empty.
        std::vector<Position> patrol;
        // The spawn guid of the leader of its creature group, or 0.
        std::uint32_t groupLeader = 0;
        // Only hostile spawns make packs. A quest corpse is not hostile.
        bool hostile = true;
    };

    // The settings of `.route generate`, with their defaults. Lengths are in
    // yards.
    struct GeneratorSettings
    {
        // How near an enemy notices a player of the dungeon level.
        float aggroRange = 20.0f;
        // A pack is on the route within aggroRange + margin of the line.
        // The pull point is that far from the pack.
        float margin = 5.0f;
        // A pull point is at most this far from its pack by navmesh path.
        float pullPathLimit = 35.0f;
        // A pack is on the level of a spot when the navmesh path from the
        // spot to it is at most levelPathFactor times the straight distance,
        // plus levelPathSlack. A longer path goes round to another level.
        float levelPathFactor = 1.5f;
        float levelPathSlack = 5.0f;
        // Spawns this close join one pack.
        float clusterDistance = 8.0f;
        // The distance between two route points along the line.
        float pointSpacing = 10.0f;
        float pointRadius = 2.0f;
        // The most that a route point moves off the line.
        float sideLimit = 3.0f;
        // An alternative point is at most this far off the line, and at least
        // alternativeGap away from the first place of its point.
        float alternativeLimit = 7.0f;
        float alternativeGap = 3.0f;
        // A room is wide when it has this much room across the line.
        float wideRoom = 10.0f;
        // Points keep this far from a wall or lava.
        float wallClearance = 1.5f;
        std::uint32_t seed = 1;
    };

    // The navmesh of the map, as the generator sees it. The game layer
    // implements it with PathInfo and the Detour query of the map.
    class RouteNav
    {
    public:
        virtual ~RouteNav() = default;
        // A walking path from `from` to `to`, the first and last position
        // included. False when no path reaches `to`, or when the path crosses
        // lava.
        virtual bool Path(Position const& from, Position const& to, std::vector<Position>& path) = 0;
        // The farthest spot on the navmesh from `from` in the flat direction
        // (dx, dy), at most `distance` away. It stops at a wall, lava, or an
        // edge. The z is the one of the navmesh there.
        virtual Position Reach(Position const& from, float dx, float dy, float distance) = 0;
        // True when the position is on the walkable navmesh, not lava.
        virtual bool OnMesh(Position const& position) = 0;
        // True when nothing of the world (vmaps) stands between the two
        // positions, at the eye height of a player.
        virtual bool LineOfSight(Position const& from, Position const& to) = 0;
    };

    // The same seed gives the same numbers on each build.
    class RouteRandom
    {
    public:
        explicit RouteRandom(std::uint32_t seed) : m_engine(seed) {}
        // A number from `low` to `high`.
        float Uniform(float low, float high);

    private:
        std::mt19937 m_engine;
    };

    // A pack: spawn indices into the spawn list, the leader first when the
    // pack has a creature group.
    struct GeneratorPack
    {
        std::vector<std::size_t> members;
    };

    // Each hostile spawn is in one pack. The packs come in the order of
    // their first spawn in the list.
    std::vector<GeneratorPack> BuildPacks(std::vector<GeneratorSpawn> const& spawns, float clusterDistance);

    // The distance from a position to the nearest spot that a member of the
    // pack can reach: its spawn point with its wander distance, or its patrol.
    float DistanceToPack(Position const& position, GeneratorPack const& pack,
        std::vector<GeneratorSpawn> const& spawns);

    // The distance along the line where it first comes within `range` of the
    // pack, or a negative number when it never does. The route trace uses it
    // as its "on the route" rule; the generator adds sight and level (#96).
    float FirstApproach(std::vector<Position> const& line, GeneratorPack const& pack,
        std::vector<GeneratorSpawn> const& spawns, float range);

    // A random sideways offset for a route point. Plus is to the left of the
    // line. `leftRoom` and `rightRoom` are the free distances on each side, a
    // wall clearance already taken off. The offset stays within the room
    // and within `limit`.
    float SidewaysOffset(float leftRoom, float rightRoom, float limit, RouteRandom& random);

    // The offsets of the alternative places of a point whose first place is
    // at `firstOffset`: none, one, or two. Only a wide room has them.
    std::vector<float> AlternativeOffsets(float leftRoom, float rightRoom, float firstOffset,
        GeneratorSettings const& settings, RouteRandom& random);

    // The packs to add for a kill count or an item: the nearest packs off the
    // route that hold `entries`, until the packs of the route and the added
    // packs hold `count` spawns of them.
    struct PackFill
    {
        std::vector<std::size_t> added; // Indices into the packs, nearest first.
        std::uint32_t have;             // The count with the added packs.
    };
    PackFill FillCount(std::vector<GeneratorPack> const& packs, std::vector<bool> const& onRoute,
        std::vector<float> const& distanceToRoute, std::vector<GeneratorSpawn> const& spawns,
        std::vector<std::uint32_t> const& entries, std::uint32_t count);

    // A spot to visit for a quest, for example the corpse that ends it.
    struct GeneratorVisit
    {
        std::string name; // Becomes the point q<quest>_<name>.
        Position position;
    };

    // A quest of the dungeon. `objectives` holds the kill and item
    // objectives; the generator adds the visit objectives of `visits`.
    struct GeneratorQuest
    {
        std::uint32_t id;
        std::vector<QuestObjective> objectives;
        std::vector<GeneratorVisit> visits;
    };

    struct GeneratorInput
    {
        std::string name;
        std::uint32_t mapId = 0;
        Position entrance{};
        std::vector<GeneratorSpawn> spawns;
        std::vector<std::uint32_t> bosses; // In the order of the run.
        std::vector<GeneratorQuest> quests;
        GeneratorSettings settings;
        // The creature name of an entry, for the names of the boss packs.
        std::function<std::string(std::uint32_t entry)> entryName;
        // The hand pack order: spawn guids, each of a mob of one pack (by
        // custom its first mob), in the order to fight the packs.
        std::vector<std::uint32_t> packOrder;
    };

    // The hand pack order file, <route>.order.txt beside the route file. One
    // line per pack, `pack <spawn guid>`, in the order to fight them. Spawn
    // guids stay the same at each regen, unlike the pack names.
    struct PackOrderResult
    {
        bool ok = false;
        std::vector<std::uint32_t> guids;
        std::string error; // "line <n>: ..." when not ok.
    };
    PackOrderResult ParsePackOrder(std::string const& text);

    // How far the route got with a quest objective.
    struct ObjectiveCheck
    {
        std::uint32_t quest;
        QuestObjective objective;
        std::uint32_t have; // Spawns of the entries on the route, or 1 for a visit.
    };

    struct GeneratedRoute
    {
        bool ok = false;
        std::string error; // When not ok.
        Route route;
        std::vector<ObjectiveCheck> objectives;
        // Things for the GM to look at in the game.
        std::vector<std::string> warnings;
    };

    GeneratedRoute GenerateRoute(GeneratorInput const& input, RouteNav& nav);

    // The checks of a route against the navmesh: each place is on the mesh,
    // and a path joins each place to the first place of the next point, with
    // no lava. Each problem is one line.
    std::vector<std::string> CheckRouteWalk(Route const& route, RouteNav& nav);

    // A name for a route step: lower case letters, digits, and `_`.
    std::string StepNameOf(std::string const& text);
}

#endif
