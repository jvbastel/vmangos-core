#ifndef WOWAI_BOTS_ROUTE_EXPORT_H
#define WOWAI_BOTS_ROUTE_EXPORT_H

#include "BotTypes.h"
#include "Route.h"
#include "RouteGenerator.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// The export of a map (`.route export <map>`, WOWAI#150): every pack of the
// map, its patrols, the quest objectives and the bosses, from the data that
// the server loaded. The dungeon map page reads it, so it works without a
// running server. It sits beside the route files as
// map_<id>.export.txt (ExportFileName), and is committed. Run the export
// again for each content patch.
//
// The packs come from the generator's own pack building (BuildPacks), so a
// pack key is the same pack in the export, the route, and the hand pack
// order file: the spawn guid of the pack's first mob.
//
// An export file looks like this:
//
//   export 389                         # map id, once, first
//   boss 11517                         # a boss entry, in run order
//   pack 48648                         # pack key: the guid of its first mob
//   mob 48648 11318 -1.25 10.50 -20.75 0.00   # guid entry x y z wander
//   mob 48650 11319 2.00 11.00 -20.00 5.00
//   wp -50.00 20.00 -30.00             # a patrol waypoint of the mob above
//   quest 5723                         # a quest; its objectives follow
//   kill 11318 8                       # kill 8 of the entry
//   item 14395 1 11322 11324           # 1 item that the entries drop
//   visit -150.00 -10.00 -60.50 Maur Grimtotem   # a spot to walk to, named
//   creature 11318 Ragefire Trogg      # names for the page: the rest of
//   questname 5723 Testing an Enemy's Strength   # the line is the name
//   itemname 14395 Spells of Shadow
//
// A name loses any `#`, since `#` starts a comment.
namespace Bots
{
    struct ExportMob
    {
        std::uint32_t guid;
        std::uint32_t entry;
        Position position;
        float wanderDistance = 0.0f;
        std::vector<Position> patrol; // The waypoints, or empty.
    };

    struct ExportPack
    {
        std::uint32_t key; // The guid of mobs.front().
        std::vector<ExportMob> mobs;
    };

    struct MapExport
    {
        std::uint32_t mapId = 0;
        std::vector<std::uint32_t> bosses;
        std::vector<ExportPack> packs;
        // The kill and item objectives, and the visit spots, of each quest.
        std::vector<GeneratorQuest> quests;
        std::map<std::uint32_t, std::string> creatureNames;
        std::map<std::uint32_t, std::string> questNames;
        std::map<std::uint32_t, std::string> itemNames;
    };

    bool operator==(ExportMob const& a, ExportMob const& b);
    bool operator==(ExportPack const& a, ExportPack const& b);
    bool operator==(MapExport const& a, MapExport const& b);

    // The export of the spawns of a map: the packs of BuildPacks (hostile
    // spawns only), in its order, with the members in its order.
    MapExport MakeExport(std::uint32_t mapId, std::vector<GeneratorSpawn> const& spawns,
        std::vector<std::uint32_t> const& bosses, std::vector<GeneratorQuest> const& quests,
        float clusterDistance);

    // The file name of the export of a map: map_<id>.export.txt.
    std::string ExportFileName(std::uint32_t mapId);

    std::string WriteExport(MapExport const& map);

    struct ExportResult
    {
        bool ok = false;
        MapExport map;     // Only when ok.
        std::string error; // "line <n>: ..." when not ok.
    };
    ExportResult ParseExport(std::string const& text);
}

#endif
