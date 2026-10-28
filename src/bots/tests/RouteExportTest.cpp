#include "RouteExport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using Bots::GeneratorSpawn;
using Bots::MapExport;

namespace
{
    // A flat open floor at z -20.5 with no walls: every path is straight.
    class OpenNav : public Bots::RouteNav
    {
    public:
        bool Path(Bots::Position const& from, Bots::Position const& to, std::vector<Bots::Position>& path) override
        {
            path = {from, to};
            return true;
        }
        Bots::Position Reach(Bots::Position const& from, float dx, float dy, float distance) override
        {
            float const norm = std::hypot(dx, dy);
            return {from.x + dx / norm * distance, from.y + dy / norm * distance, from.z};
        }
        bool OnMesh(Bots::Position const&) override { return true; }
        bool LineOfSight(Bots::Position const&, Bots::Position const&) override { return true; }
    };

    GeneratorSpawn Spawn(std::uint32_t guid, std::uint32_t entry, float x, float y, float wander = 0.0f,
        std::uint32_t leader = 0)
    {
        GeneratorSpawn spawn{guid, entry, {x, y, -20.5f}};
        spawn.wanderDistance = wander;
        spawn.groupLeader = leader;
        return spawn;
    }

    // A map with a creature group, a close pair, a wanderer, a patrol, a
    // quest corpse, a boss, and quests of each objective kind.
    MapExport SampleExport()
    {
        MapExport map;
        map.mapId = 389;
        map.bosses = {11517, 11520};
        map.packs = {
            {48648, {{48648, 11318, {-1.25f, 10.5f, -20.75f}, 0.0f, {}}, {48650, 11319, {2.0f, 11.0f, -20.0f}, 5.0f, {}}}},
            {48700, {{48700, 11517, {-50.0f, 20.0f, -30.0f}, 0.0f, {{-50.0f, 20.0f, -30.0f}, {-60.25f, 25.5f, -31.0f}}}}},
        };
        Bots::GeneratorQuest quest{5723, {}, {}};
        Bots::QuestObjective kill{Bots::QuestObjectiveKind::Kill};
        kill.entry = 11318;
        kill.count = 8;
        Bots::QuestObjective item{Bots::QuestObjectiveKind::Item};
        item.item = 14395;
        item.count = 1;
        item.entries = {11322, 11324};
        quest.objectives = {kill, item};
        Bots::GeneratorQuest visit{5722, {}, {{"Maur Grimtotem", {-150.0f, -10.0f, -60.5f}}}};
        map.quests = {quest, visit};
        map.creatureNames = {{11318, "Ragefire Trogg"}, {11517, "Oggleflint"}};
        map.questNames = {{5723, "Testing an Enemy's Strength"}};
        map.itemNames = {{14395, "Spells of Shadow"}};
        return map;
    }
}

TEST(RouteExport, AnExportWrittenAndReadBackIsTheSame)
{
    MapExport const map = SampleExport();

    Bots::ExportResult const read = Bots::ParseExport(Bots::WriteExport(map));

    ASSERT_TRUE(read.ok) << read.error;
    EXPECT_EQ(read.map, map);
}

TEST(RouteExport, PackKeysAreTheFirstMobsOfTheGeneratorsPacks)
{
    // Along a corridor: a close pair, a creature group whose leader comes
    // after its member in the spawn list, and the boss.
    std::vector<GeneratorSpawn> const spawns = {
        Spawn(10, 11318, 20, 5),
        Spawn(11, 11318, 40, 5, 0.0f, 13),
        Spawn(12, 11319, 25, 5),
        Spawn(13, 11319, 45, 8),
        Spawn(20, 11517, 80, 0),
    };
    Bots::GeneratorInput input;
    input.name = "corridor";
    input.mapId = 389;
    input.entrance = {0.0f, 0.0f, -20.5f};
    input.spawns = spawns;
    input.bosses = {11517};

    MapExport const map = Bots::MakeExport(389, spawns, input.bosses, {}, input.settings.clusterDistance);
    OpenNav nav;
    Bots::GeneratedRoute const generated = Bots::GenerateRoute(input, nav);

    std::vector<std::uint32_t> exportKeys;
    for (Bots::ExportPack const& pack : map.packs)
        exportKeys.push_back(pack.key);
    std::sort(exportKeys.begin(), exportKeys.end());
    EXPECT_EQ(exportKeys, (std::vector<std::uint32_t>{10, 13, 20}));

    ASSERT_TRUE(generated.ok) << generated.error;
    std::vector<std::uint32_t> routeKeys;
    for (Bots::RoutePack const& pack : generated.route.packs)
        routeKeys.push_back(pack.mobs.front().spawnGuid);
    std::sort(routeKeys.begin(), routeKeys.end());
    EXPECT_EQ(routeKeys, exportKeys);

    // The mobs of each pack, in the same order.
    for (Bots::RoutePack const& routePack : generated.route.packs)
    {
        auto exported = std::find_if(map.packs.begin(), map.packs.end(),
            [&](Bots::ExportPack const& p) { return p.key == routePack.mobs.front().spawnGuid; });
        ASSERT_NE(exported, map.packs.end());
        ASSERT_EQ(exported->mobs.size(), routePack.mobs.size());
        for (std::size_t i = 0; i < routePack.mobs.size(); ++i)
            EXPECT_EQ(exported->mobs[i].guid, routePack.mobs[i].spawnGuid);
    }
}

TEST(RouteExport, AFileWithoutTheExportLineIsAnError)
{
    Bots::ExportResult const read = Bots::ParseExport("pack 1\nmob 1 2 0 0 0 0\n");

    EXPECT_FALSE(read.ok);
    EXPECT_EQ(read.error, "line 1: the file starts with: export <map id>");
}

TEST(RouteExport, APackKeyIsTheGuidOfItsFirstMob)
{
    Bots::ExportResult const read = Bots::ParseExport("export 389\npack 7\nmob 8 2 0 0 0 0\n");

    EXPECT_FALSE(read.ok);
    EXPECT_EQ(read.error, "line 3: the first mob of the pack 7 has another guid");
}
