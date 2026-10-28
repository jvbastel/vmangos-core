#include "BotGeometry.h"
#include "RouteGenerator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

using Bots::Distance2d;
using Bots::GeneratorPack;
using Bots::GeneratorSpawn;
using Bots::Position;

namespace
{
    GeneratorSpawn Spawn(std::uint32_t guid, std::uint32_t entry, float x, float y, float wander = 0.0f,
        std::uint32_t leader = 0)
    {
        GeneratorSpawn spawn{guid, entry, {x, y, 0.0f}};
        spawn.wanderDistance = wander;
        spawn.groupLeader = leader;
        return spawn;
    }

    std::vector<std::vector<std::uint32_t>> GuidsOf(std::vector<GeneratorPack> const& packs,
        std::vector<GeneratorSpawn> const& spawns)
    {
        std::vector<std::vector<std::uint32_t>> guids;
        for (GeneratorPack const& pack : packs)
        {
            guids.emplace_back();
            for (std::size_t member : pack.members)
                guids.back().push_back(spawns[member].guid);
        }
        return guids;
    }

    struct Rect
    {
        float x0, x1, y0, y1;
        bool Holds(float x, float y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
    };

    // A flat navmesh at z 0: the union of the rooms, without the lava. A path
    // is the straight line when the whole line is walkable, else a line
    // through one of the doors.
    class FakeNav : public Bots::RouteNav
    {
    public:
        std::vector<Rect> rooms;
        std::vector<Rect> lava;
        std::vector<Position> doors;
        // Rock that blocks the line of sight, walkable or not.
        std::vector<Rect> rock;

        bool LineOfSight(Position const& from, Position const& to) override
        {
            float const length = std::hypot(to.x - from.x, to.y - from.y);
            int const steps = std::max(1, int(length / 0.5f));
            for (int i = 0; i <= steps; ++i)
            {
                float const t = float(i) / float(steps);
                for (Rect const& r : rock)
                    if (r.Holds(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t))
                        return false;
            }
            return true;
        }

        bool OnMesh(Position const& p) override
        {
            for (Rect const& r : lava)
                if (r.Holds(p.x, p.y))
                    return false;
            for (Rect const& r : rooms)
                if (r.Holds(p.x, p.y))
                    return true;
            return false;
        }

        bool Path(Position const& from, Position const& to, std::vector<Position>& path) override
        {
            if (Straight(from, to, path))
                return true;
            std::vector<Position> second;
            for (Position const& door : doors)
                if (Straight(from, door, path) && Straight(door, to, second))
                {
                    path.push_back(second.back());
                    return true;
                }
            return false;
        }

        bool Straight(Position const& from, Position const& to, std::vector<Position>& path)
        {
            float const length = Bots::Distance2d(from, to);
            int const steps = std::max(1, int(length / 0.5f));
            for (int i = 0; i <= steps; ++i)
            {
                float const t = float(i) / float(steps);
                if (!OnMesh({from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, 0.0f}))
                    return false;
            }
            path = {{from.x, from.y, 0.0f}, {to.x, to.y, 0.0f}};
            return true;
        }

        Position Reach(Position const& from, float dx, float dy, float distance) override
        {
            float const norm = std::hypot(dx, dy);
            Position last{from.x, from.y, 0.0f};
            for (float d = 0.1f; d <= distance + 1e-4f; d += 0.1f)
            {
                Position const next{from.x + dx / norm * d, from.y + dy / norm * d, 0.0f};
                if (!OnMesh(next))
                    break;
                last = next;
            }
            return last;
        }
    };
}

TEST(RoutePacks, CreatureGroupsAndCloseSpawnsMakeOnePack)
{
    std::vector<GeneratorSpawn> spawns = {
        Spawn(10, 11318, 0, 0),
        Spawn(11, 11318, 30, 0, 0, 13), // A member far from its leader.
        Spawn(12, 11319, 6, 0),         // Close to 10.
        Spawn(13, 11318, 50, 0),        // The leader of 11.
        Spawn(14, 11319, 13, 0),        // Close to 12: a chain.
        Spawn(15, 11318, 100, 0),       // Alone.
    };
    GeneratorSpawn corpse = Spawn(16, 11834, 101, 0);
    corpse.hostile = false;
    spawns.push_back(corpse);

    std::vector<GeneratorPack> packs = Bots::BuildPacks(spawns, 8.0f);

    EXPECT_EQ(GuidsOf(packs, spawns),
        (std::vector<std::vector<std::uint32_t>>{{10, 12, 14}, {13, 11}, {15}}));
}

TEST(RouteOnTheRoute, WanderAndPatrolsBringAPackNearer)
{
    std::vector<GeneratorSpawn> spawns = {Spawn(1, 11318, 0, 30, 6.0f), Spawn(2, 11318, 0, 40)};
    spawns[1].patrol = {{-10, 40, 0}, {-10, 5, 0}};
    GeneratorPack wanderer{{0}};
    GeneratorPack patroller{{1}};

    EXPECT_FLOAT_EQ(Bots::DistanceToPack({0, 0, 0}, wanderer, spawns), 24.0f);
    EXPECT_FLOAT_EQ(Bots::DistanceToPack({0, 0, 0}, patroller, spawns), std::hypot(10.0f, 5.0f));
    EXPECT_FLOAT_EQ(Bots::DistanceToPack({0, 35, 0}, wanderer, spawns), 0.0f);
}

TEST(RouteOnTheRoute, APackIsOnTheRouteWhereTheLineFirstComesInRange)
{
    std::vector<GeneratorSpawn> spawns = {Spawn(1, 11318, 60, 0), Spawn(2, 11318, 40, 30, 6.0f),
        Spawn(3, 11318, 50, 40)};
    std::vector<Position> const line = {{0, 0, 0}, {100, 0, 0}};

    float const near = Bots::FirstApproach(line, GeneratorPack{{0}}, spawns, 25.0f);
    float const wanderer = Bots::FirstApproach(line, GeneratorPack{{1}}, spawns, 25.0f);
    float const far = Bots::FirstApproach(line, GeneratorPack{{2}}, spawns, 25.0f);

    EXPECT_NEAR(near, 35.0f, 1.0f);
    // sqrt((x - 40)^2 + 30^2) - 6 = 25 at x = 40 - 7.8.
    EXPECT_NEAR(wanderer, 32.2f, 1.0f);
    EXPECT_LT(far, 0.0f);
}

TEST(RouteVariance, TheSidewaysOffsetStaysInTheRoomAndTheLimit)
{
    Bots::RouteRandom random(7);
    bool left = false;
    bool right = false;
    for (int i = 0; i < 200; ++i)
    {
        float const offset = Bots::SidewaysOffset(10.0f, 1.0f, 3.0f, random);
        EXPECT_LE(offset, 3.0f);
        EXPECT_GE(offset, -1.0f);
        left = left || offset > 1.5f;
        right = right || offset < -0.5f;
    }
    EXPECT_TRUE(left);
    EXPECT_TRUE(right);
    EXPECT_FLOAT_EQ(Bots::SidewaysOffset(0.0f, 0.0f, 3.0f, random), 0.0f);
}

TEST(RouteVariance, TheSameSeedGivesTheSameOffsets)
{
    Bots::RouteRandom a(42);
    Bots::RouteRandom b(42);
    Bots::RouteRandom c(43);
    std::vector<float> first, second, third;
    for (int i = 0; i < 10; ++i)
    {
        first.push_back(Bots::SidewaysOffset(5.0f, 5.0f, 3.0f, a));
        second.push_back(Bots::SidewaysOffset(5.0f, 5.0f, 3.0f, b));
        third.push_back(Bots::SidewaysOffset(5.0f, 5.0f, 3.0f, c));
    }
    EXPECT_EQ(first, second);
    EXPECT_NE(first, third);
}

TEST(RouteVariance, OnlyAWideRoomHasAlternatives)
{
    Bots::GeneratorSettings settings;
    Bots::RouteRandom random(1);

    EXPECT_TRUE(Bots::AlternativeOffsets(2.0f, 2.0f, 0.5f, settings, random).empty());

    std::vector<float> both = Bots::AlternativeOffsets(12.0f, 12.0f, 1.0f, settings, random);
    ASSERT_EQ(both.size(), 2u);
    EXPECT_GE(both[0], 1.0f + settings.alternativeGap);
    EXPECT_LE(both[0], settings.alternativeLimit);
    EXPECT_LE(both[1], 1.0f - settings.alternativeGap);
    EXPECT_GE(both[1], -settings.alternativeLimit);

    // Room on one side only.
    std::vector<float> one = Bots::AlternativeOffsets(12.0f, 1.0f, 0.0f, settings, random);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_GE(one[0], settings.alternativeGap);
}

TEST(RouteQuestFill, AddsTheNearestPacksWithTheEntryUntilTheCountIsMet)
{
    std::vector<GeneratorSpawn> spawns = {Spawn(1, 11318, 0, 0), Spawn(2, 11318, 0, 0), Spawn(3, 11319, 0, 0),
        Spawn(4, 11318, 0, 0), Spawn(5, 11318, 0, 0), Spawn(6, 11318, 0, 0)};
    std::vector<GeneratorPack> packs = {
        {{0, 1}}, // On the route: 2 trogg.
        {{2}},    // Off, near, no trogg.
        {{3}},    // Off, far.
        {{4, 5}}, // Off, nearer.
    };
    std::vector<bool> const onRoute = {true, false, false, false};
    std::vector<float> const distance = {0.0f, 10.0f, 80.0f, 40.0f};

    Bots::PackFill met = Bots::FillCount(packs, onRoute, distance, spawns, {11318}, 2);
    EXPECT_TRUE(met.added.empty());
    EXPECT_EQ(met.have, 2u);

    Bots::PackFill four = Bots::FillCount(packs, onRoute, distance, spawns, {11318}, 4);
    EXPECT_EQ(four.added, (std::vector<std::size_t>{3}));
    EXPECT_EQ(four.have, 4u);

    Bots::PackFill short_ = Bots::FillCount(packs, onRoute, distance, spawns, {11318}, 9);
    EXPECT_EQ(short_.added, (std::vector<std::size_t>{3, 2}));
    EXPECT_EQ(short_.have, 5u);

    Bots::PackFill item = Bots::FillCount(packs, onRoute, distance, spawns, {11319, 99}, 1);
    EXPECT_EQ(item.added, (std::vector<std::size_t>{1}));
}

TEST(RouteNames, StepNamesAreLowerCaseWords)
{
    EXPECT_EQ(Bots::StepNameOf("Taragaman the Hungerer"), "taragaman_the_hungerer");
    EXPECT_EQ(Bots::StepNameOf("Maur Grimtotem's Corpse"), "maur_grimtotems_corpse");
}

namespace
{
    // A narrow corridor from x 0 to 100, then a wide room to x 200, and a side
    // corridor up from the room. A lava pool lies in the room.
    FakeNav Dungeon()
    {
        FakeNav nav;
        nav.rooms = {{0, 100, -3, 3}, {100, 200, -15, 15}, {118, 122, 15, 80}};
        nav.lava = {{160, 170, -15, -8}};
        nav.doors = {{120, 10, 0}};
        return nav;
    }

    Bots::GeneratorInput DungeonInput(std::uint32_t seed)
    {
        Bots::GeneratorInput input;
        input.name = "test_dungeon";
        input.mapId = 389;
        input.entrance = {2, 0, 0};
        input.spawns = {
            Spawn(1, 11318, 60, 0),       // On the line.
            Spawn(2, 11319, 40, 30, 6.0f), // Off the line, but it wanders near.
            Spawn(3, 11318, 120, 60),     // Far up the side corridor.
            Spawn(4, 11319, 30, -80),     // Far away, never on the route.
            Spawn(5, 100, 150, 0),        // The first boss.
            Spawn(6, 200, 195, 10),       // The second boss.
        };
        input.bosses = {100, 200};
        Bots::GeneratorQuest kill{5723, {}, {}};
        Bots::QuestObjective trogg{Bots::QuestObjectiveKind::Kill};
        trogg.entry = 11318;
        trogg.count = 2;
        kill.objectives.push_back(trogg);
        Bots::GeneratorQuest visit{5722, {}, {{"Corpse", {152, 5, 0}}}};
        input.quests = {visit, kill};
        input.settings.seed = seed;
        input.entryName = [](std::uint32_t entry) {
            return entry == 100 ? std::string("Boss One") : entry == 200 ? std::string("Boss Two") : std::string();
        };
        return input;
    }

    std::vector<std::string> PackNames(Bots::Route const& route)
    {
        std::vector<std::string> names;
        for (Bots::RouteStep const& step : route.steps)
            if (step.kind == Bots::RouteStepKind::Pack)
                names.push_back(route.packs[step.index].name);
        return names;
    }
}

TEST(RouteGenerator, FightsThePacksInTheOrderTheLineReachesThem)
{
    FakeNav nav = Dungeon();
    Bots::GeneratedRoute generated = Bots::GenerateRoute(DungeonInput(1), nav);

    ASSERT_TRUE(generated.ok) << generated.error;
    Bots::Route const& route = generated.route;
    EXPECT_EQ(route.name, "test_dungeon");
    EXPECT_EQ(route.mapId, 389u);
    EXPECT_EQ(route.bosses, (std::vector<std::uint32_t>{100, 200}));
    EXPECT_TRUE(route.hasSeed);
    EXPECT_EQ(route.seed, 1u);
    EXPECT_EQ(PackNames(route), (std::vector<std::string>{"pack1", "pack2", "pack3", "boss_one", "boss_two"}));
    EXPECT_EQ(Bots::FindPack(route, "pack1")->mobs[0].spawnGuid, 2u);
    EXPECT_EQ(Bots::FindPack(route, "pack2")->mobs[0].spawnGuid, 1u);
    EXPECT_EQ(Bots::FindPack(route, "pack3")->mobs[0].spawnGuid, 3u);
    EXPECT_EQ(Bots::FindPack(route, "boss_two")->mobs[0].entry, 200u);
    for (Bots::RoutePack const& pack : route.packs)
        for (Bots::RouteMob const& mob : pack.mobs)
            EXPECT_NE(mob.spawnGuid, 4u) << "the far pack is not on the route";
}

TEST(RouteGenerator, EachPackHasAPullPointOutsideItsAggroRange)
{
    FakeNav nav = Dungeon();
    Bots::GeneratorInput const input = DungeonInput(1);
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;
    Bots::Route const& route = generated.route;

    std::vector<GeneratorPack> const packs = Bots::BuildPacks(input.spawns, input.settings.clusterDistance);
    for (std::size_t i = 0; i < route.steps.size(); ++i)
    {
        if (route.steps[i].kind != Bots::RouteStepKind::Pack)
            continue;
        Bots::RoutePack const& pack = route.packs[route.steps[i].index];
        ASSERT_GT(i, 0u);
        ASSERT_EQ(route.steps[i - 1].kind, Bots::RouteStepKind::Point) << pack.name;
        Bots::RoutePoint const& pull = route.points[route.steps[i - 1].index];
        EXPECT_EQ(pull.name, "pull_" + pack.name);
        GeneratorPack spawnPack;
        for (std::size_t s = 0; s < input.spawns.size(); ++s)
            for (Bots::RouteMob const& mob : pack.mobs)
                if (input.spawns[s].guid == mob.spawnGuid)
                    spawnPack.members.push_back(s);
        for (Bots::RoutePlace const& place : pull.places)
        {
            float const distance = Bots::DistanceToPack(place.position, spawnPack, input.spawns);
            EXPECT_GE(distance, input.settings.aggroRange) << pull.name;
            EXPECT_LE(distance, input.settings.aggroRange + input.settings.margin + 2.0f) << pull.name;
        }
    }
}

TEST(RouteGenerator, PointsSitOffTheLineOnTheMeshAndWideRoomsHaveAlternatives)
{
    FakeNav nav = Dungeon();
    Bots::GeneratedRoute generated = Bots::GenerateRoute(DungeonInput(1), nav);
    ASSERT_TRUE(generated.ok) << generated.error;
    Bots::Route const& route = generated.route;

    bool offTheLine = false;
    bool alternativeInTheRoom = false;
    for (Bots::RoutePoint const& point : route.points)
    {
        Position const first = point.places[0].position;
        EXPECT_FLOAT_EQ(point.places[0].radius, 2.0f);
        for (Bots::RoutePlace const& place : point.places)
            EXPECT_TRUE(nav.OnMesh(place.position)) << point.name;
        if (point.quest == 0 && first.x < 95.0f)
        {
            // The corridor: the line is y 0, and it is too narrow for alternatives.
            EXPECT_LE(std::fabs(first.y), 3.0f + 1e-3f) << point.name;
            EXPECT_EQ(point.places.size(), 1u) << point.name;
            offTheLine = offTheLine || std::fabs(first.y) > 0.05f;
        }
        if (point.quest == 0 && first.x > 105.0f && first.x < 145.0f && point.name.compare(0, 5, "pull_") != 0)
            alternativeInTheRoom = alternativeInTheRoom || point.places.size() > 1;
    }
    EXPECT_TRUE(offTheLine);
    EXPECT_TRUE(alternativeInTheRoom);
    std::vector<std::string> const problems = Bots::CheckRouteWalk(route, nav);
    EXPECT_TRUE(problems.empty()) << problems.front();
}

TEST(RouteGenerator, TheSameSeedGivesTheSameFile)
{
    FakeNav nav = Dungeon();
    std::string const one = Bots::WriteRoute(Bots::GenerateRoute(DungeonInput(5), nav).route, nullptr);
    std::string const again = Bots::WriteRoute(Bots::GenerateRoute(DungeonInput(5), nav).route, nullptr);
    std::string const other = Bots::WriteRoute(Bots::GenerateRoute(DungeonInput(6), nav).route, nullptr);

    EXPECT_EQ(one, again);
    EXPECT_NE(one, other);
    EXPECT_NE(one.find("seed 5\n"), std::string::npos);
    Bots::RouteResult parsed = Bots::ParseRoute(one);
    ASSERT_TRUE(parsed.ok) << parsed.error;
}

TEST(RouteGenerator, TheRouteMeetsTheKillCountsAndVisitsTheQuestSpots)
{
    FakeNav nav = Dungeon();
    Bots::GeneratedRoute generated = Bots::GenerateRoute(DungeonInput(1), nav);
    ASSERT_TRUE(generated.ok) << generated.error;
    Bots::Route const& route = generated.route;

    ASSERT_EQ(route.quests.size(), 2u);
    Bots::RouteQuest const* visit = Bots::FindQuest(route, 5722);
    ASSERT_NE(visit, nullptr);
    ASSERT_EQ(visit->objectives.size(), 1u);
    EXPECT_EQ(visit->objectives[0].kind, Bots::QuestObjectiveKind::Visit);
    Bots::RoutePoint const* corpse = Bots::FindPoint(route, visit->objectives[0].point);
    ASSERT_NE(corpse, nullptr);
    EXPECT_EQ(corpse->name, "q5722_corpse");
    EXPECT_EQ(corpse->quest, 5722u);
    EXPECT_LE(Distance2d(corpse->places[0].position, {152, 5, 0}), 0.01f);

    // The second trogg is up the side corridor: its pack and pull point are
    // for the quest, and so are the points of the way there.
    Bots::RoutePack const* trogg = Bots::FindPack(route, "pack3");
    ASSERT_NE(trogg, nullptr);
    EXPECT_EQ(trogg->quest, 5723u);
    EXPECT_EQ(Bots::FindPack(route, "pack2")->quest, 0u);
    EXPECT_EQ(Bots::FindPoint(route, "pull_pack3")->quest, 5723u);
    EXPECT_GT(Bots::FindPoint(route, "pull_pack3")->places[0].position.y, 15.0f);

    bool checked = false;
    for (Bots::ObjectiveCheck const& check : generated.objectives)
        if (check.quest == 5723)
        {
            EXPECT_GE(check.have, check.objective.count);
            checked = true;
        }
    EXPECT_TRUE(checked);
}

TEST(RouteGenerator, AMissingBossOrNoPathIsAnError)
{
    FakeNav nav = Dungeon();
    Bots::GeneratorInput input = DungeonInput(1);
    input.bosses.push_back(300);
    EXPECT_EQ(Bots::GenerateRoute(input, nav).error, "the map has no spawn of the boss 300");

    input = DungeonInput(1);
    input.spawns[5].position = {195, 40, 0}; // Off the mesh.
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    EXPECT_FALSE(generated.ok);
    EXPECT_EQ(generated.error.find("no navmesh path to the boss 200"), 0u) << generated.error;
}

TEST(RouteWalkCheck, FindsAPlaceOffTheMeshAndAPathThroughLava)
{
    FakeNav nav = Dungeon();
    nav.doors.clear(); // Only the straight path, through the lava.
    Bots::Route route = Bots::ParseRoute(
        "route t 1\n"
        "point a 150 -10 0 2\n"   // Before the lava pool.
        "point b 180 -10 0 2\n"   // After it: the straight path crosses the lava.
        "alt 180 -40 0 2\n").route; // Off the mesh.

    std::vector<std::string> const problems = Bots::CheckRouteWalk(route, nav);

    EXPECT_EQ(problems, (std::vector<std::string>{
        "point b: the place 2 at 180.00 -40.00 0.00 is not on the navmesh",
        "point a: no path from the place 1 to the point b",
    }));
}

TEST(RouteGenerator, TwoPacksAtOneSpotEachGetTheirPullPointRightBeforeThem)
{
    FakeNav nav = Dungeon();
    Bots::GeneratorInput input = DungeonInput(1);
    // Both sides of the corridor, as far from the line: the line reaches
    // both at one spot.
    input.spawns.push_back(Spawn(7, 11318, 70, 9));
    input.spawns.push_back(Spawn(8, 11318, 70, -9));
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;
    Bots::Route const& route = generated.route;

    for (std::size_t i = 0; i < route.steps.size(); ++i)
        if (route.steps[i].kind == Bots::RouteStepKind::Pack)
        {
            ASSERT_EQ(route.steps[i - 1].kind, Bots::RouteStepKind::Point);
            EXPECT_EQ(route.points[route.steps[i - 1].index].name, "pull_" + route.packs[route.steps[i].index].name);
        }
}

TEST(RouteGenerator, AQuestVisitComesAfterThePacksThatGuardTheSpot)
{
    FakeNav nav = Dungeon();
    Bots::GeneratorInput input = DungeonInput(1);
    // A pack off the line, but within aggro range of the spot to visit.
    input.spawns.push_back(Spawn(9, 11319, 120, 40));
    input.quests[0].visits[0].position = {120, 22, 0};
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;
    Bots::Route const& route = generated.route;

    std::size_t guard = route.steps.size();
    std::size_t visit = route.steps.size();
    for (std::size_t i = 0; i < route.steps.size(); ++i)
    {
        Bots::RouteStep const& step = route.steps[i];
        if (step.kind == Bots::RouteStepKind::Pack && route.packs[step.index].mobs[0].spawnGuid == 9)
        {
            guard = i;
            EXPECT_EQ(route.packs[step.index].quest, 5722u);
        }
        if (step.kind == Bots::RouteStepKind::Point && route.points[step.index].name == "q5722_corpse")
            visit = i;
    }
    ASSERT_LT(guard, route.steps.size());
    ASSERT_LT(visit, route.steps.size());
    EXPECT_LT(guard, visit);
}

namespace
{
    // Two levels: the line runs along an upper corridor, and a lower room
    // lies beside it, joined to it only by a ramp at its far end. A path
    // goes straight, or down the ramp.
    class LevelsNav : public FakeNav
    {
    public:
        LevelsNav()
        {
            rooms = {{0, 200, -3, 3}, {96, 98, -3, 20}, {40, 98, 10, 30}};
            doors = {{97, 0, 0}, {97, 15, 0}};
        }

        bool Path(Position const& from, Position const& to, std::vector<Position>& path) override
        {
            if (Straight(from, to, path))
                return true;
            // Down the ramp, or up it.
            for (bool down : {true, false})
            {
                std::vector<Position> stops{from};
                if (down)
                    stops.insert(stops.end(), doors.begin(), doors.end());
                else
                    stops.insert(stops.end(), doors.rbegin(), doors.rend());
                stops.push_back(to);
                std::vector<Position> leg;
                bool walkable = true;
                for (std::size_t i = 0; i + 1 < stops.size(); ++i)
                    walkable = walkable && Straight(stops[i], stops[i + 1], leg);
                if (walkable)
                {
                    path = stops;
                    return true;
                }
            }
            return false;
        }
    };

    Bots::GeneratorInput LevelsInput(float packX, float packY = 15)
    {
        Bots::GeneratorInput input;
        input.name = "levels";
        input.mapId = 389;
        input.entrance = {2, 0, 0};
        input.spawns = {Spawn(1, 11318, packX, packY), Spawn(5, 100, 190, 0)};
        input.bosses = {100};
        return input;
    }
}

TEST(RouteGenerator, APullPointIsNearItsPackByPathNotByStraightDistance)
{
    LevelsNav nav;
    Bots::GeneratorInput const input = LevelsInput(80);
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;

    Bots::RoutePoint const* pull = Bots::FindPoint(generated.route, "pull_pack1");
    ASSERT_NE(pull, nullptr);
    for (Bots::RoutePlace const& place : pull->places)
    {
        std::vector<Position> path;
        ASSERT_TRUE(nav.Path(place.position, {80, 15, 0}, path));
        EXPECT_LE(Bots::PathLength(path), input.settings.pullPathLimit) << place.position.x;
        EXPECT_GE(Bots::Distance3d(place.position, {80, 15, 0}), input.settings.aggroRange);
    }
    for (std::string const& warning : generated.warnings)
        EXPECT_EQ(warning.find("pull point"), std::string::npos) << warning;
}

namespace
{
    bool Fights(Bots::Route const& route, std::uint32_t guid)
    {
        for (Bots::RoutePack const& pack : route.packs)
            if (pack.mobs[0].spawnGuid == guid)
                return true;
        return false;
    }

    Bots::GeneratorQuest KillQuest(std::uint32_t entry, std::uint32_t count)
    {
        Bots::GeneratorQuest kill{5723, {}, {}};
        Bots::QuestObjective objective{Bots::QuestObjectiveKind::Kill};
        objective.entry = entry;
        objective.count = count;
        kill.objectives.push_back(objective);
        return kill;
    }

    bool Told(Bots::GeneratedRoute const& generated, std::string const& text)
    {
        for (std::string const& warning : generated.warnings)
            if (warning.find(text) != std::string::npos)
                return true;
        return false;
    }
}

TEST(RouteGenerator, APackOnAnotherLevelIsLeftOutWithTheReason)
{
    // The lower room's far end: 15 yards from the line, but from the line
    // the only way is down the ramp and back along the room, 72 yards.
    LevelsNav nav;
    Bots::GeneratedRoute generated = Bots::GenerateRoute(LevelsInput(40), nav);
    ASSERT_TRUE(generated.ok) << generated.error;

    EXPECT_FALSE(Fights(generated.route, 1));
    for (Bots::RoutePoint const& point : generated.route.points)
        EXPECT_LT(point.places[0].position.y, 5.0f) << point.name << ": no detour goes down to it";
    EXPECT_TRUE(Told(generated, "the pack of spawn 1 at 40.00 15.00 0.00 is off the route: it is on another level"));
    // No point keeps away from a pack that cannot see it.
    EXPECT_FALSE(Told(generated, "is within aggro range of a pack"));
}

TEST(RouteGenerator, APackOutOfSightOfTheLineIsLeftOutWithTheReason)
{
    // Near the ramp, so near by path, but rock stands between it and the
    // line.
    LevelsNav nav;
    nav.rock = {{40, 95, 4, 9}};
    Bots::GeneratedRoute generated = Bots::GenerateRoute(LevelsInput(80), nav);
    ASSERT_TRUE(generated.ok) << generated.error;

    EXPECT_FALSE(Fights(generated.route, 1));
    EXPECT_TRUE(Told(generated, "the pack of spawn 1 at 80.00 15.00 0.00 is off the route: no line of sight"));
}

TEST(RouteGenerator, APackOnAnotherLevelIsOnTheRouteWhenTheLineGoesThere)
{
    // The boss is in the lower room: the line goes down the ramp and
    // passes the pack there.
    LevelsNav nav;
    Bots::GeneratorInput input = LevelsInput(40);
    input.spawns[1].position = {45, 28, 0};
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;

    ASSERT_TRUE(Fights(generated.route, 1));
    Bots::RoutePoint const* pull = Bots::FindPoint(generated.route, "pull_pack1");
    ASSERT_NE(pull, nullptr);
    EXPECT_GT(pull->places[0].position.y, 10.0f) << "the pull point is in the lower room";
    EXPECT_FALSE(Told(generated, "is within aggro range of a pack"));
    EXPECT_EQ(Bots::CheckRouteWalk(generated.route, nav), std::vector<std::string>{});
}

TEST(RouteGenerator, APackOutOfAggroRangeOfTheLineAndFarByPathIsNotOnTheRoute)
{
    // 25 yards below the line, so in range plus the margin, and 82 yards
    // away by path.
    LevelsNav nav;
    Bots::GeneratedRoute generated = Bots::GenerateRoute(LevelsInput(40, 25), nav);
    ASSERT_TRUE(generated.ok) << generated.error;

    EXPECT_FALSE(Fights(generated.route, 1));
    EXPECT_TRUE(Told(generated, "the pack of spawn 1 at 40.00 25.00 0.00 is off the route"));
}

TEST(RouteGenerator, AQuestDetourFightsThePacksItPassesThatCanSeeIt)
{
    // A quest needs the trogg at the far end of the lower room. The detour
    // passes a pack in the room, and a pack behind rock.
    LevelsNav nav;
    nav.rooms.push_back({40, 98, 30, 40});
    nav.rock = {{50, 75, 22, 30}};
    Bots::GeneratorInput input = LevelsInput(40);
    input.spawns.push_back(Spawn(2, 11319, 85, 29));
    input.spawns.push_back(Spawn(3, 11319, 63, 36));
    input.quests = {KillQuest(11318, 1)};
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;

    std::vector<std::uint32_t> fought;
    for (std::size_t i = 0; i < generated.route.steps.size(); ++i)
        if (generated.route.steps[i].kind == Bots::RouteStepKind::Pack)
        {
            Bots::RoutePack const& pack = generated.route.packs[generated.route.steps[i].index];
            fought.push_back(pack.mobs[0].spawnGuid);
            ASSERT_EQ(generated.route.steps[i - 1].kind, Bots::RouteStepKind::Point);
            EXPECT_EQ(generated.route.points[generated.route.steps[i - 1].index].name, "pull_" + pack.name);
        }
    EXPECT_EQ(fought, (std::vector<std::uint32_t>{2, 1, 5}));
}

TEST(RouteGenerator, APullPointCountsThePathToTheNearestMember)
{
    // A pack of two along the lower room: the leader at its far end, the
    // other member near the ramp.
    LevelsNav nav;
    Bots::GeneratorInput input = LevelsInput(40);
    input.spawns.push_back(Spawn(2, 11318, 82, 15, 0.0f, 1));
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;

    EXPECT_TRUE(Fights(generated.route, 1));
    EXPECT_FALSE(Told(generated, "no pull point within"));
}

TEST(RouteGenerator, AQuestDetourStartsWhereThePathFromTheLineIsShortest)
{
    // The quest trogg is at the lower room's far end: the detour goes down
    // the ramp and along the room, not first back along the corridor.
    LevelsNav nav;
    Bots::GeneratorInput input = LevelsInput(40);
    input.quests = {KillQuest(11318, 1)};
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;

    ASSERT_TRUE(Fights(generated.route, 1));
    EXPECT_TRUE(Told(generated, "the pack of spawn 1 at 40.00 15.00 0.00 is off the route: it is on another level "
                                "(the line passes it at 15 yards, but the shortest path from the line to it is 109 "
                                "yards); a detour for quest 5723 fights it"));
    for (Bots::RoutePoint const& point : generated.route.points)
        if (point.quest == 5723)
            EXPECT_TRUE(point.places[0].position.x > 90.0f || point.places[0].position.y > 5.0f)
                << point.name << " at " << point.places[0].position.x;
}

TEST(RouteGenerator, AQuestTakesThePackNearestToTheLineByPath)
{
    // Two troggs off the line: one in the lower room, 15 yards from the
    // line but 72 by path, and one up a side room, 40 yards by both.
    LevelsNav nav;
    nav.rooms.push_back({0, 10, 3, 45});
    Bots::GeneratorInput input = LevelsInput(40);
    input.spawns.push_back(Spawn(2, 11318, 5, 40));
    input.quests = {KillQuest(11318, 1)};
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;

    EXPECT_TRUE(Fights(generated.route, 2));
    EXPECT_FALSE(Fights(generated.route, 1));
}

TEST(RoutePackOrder, ReadsOnePackGuidPerLineWithComments)
{
    Bots::PackOrderResult const order = Bots::ParsePackOrder(
        "# The trogg room first.\n"
        "pack 48648 # pack7, on the ramp\n"
        "\n"
        "pack 48653\n");
    ASSERT_TRUE(order.ok) << order.error;
    EXPECT_EQ(order.guids, (std::vector<std::uint32_t>{48648, 48653}));
    EXPECT_TRUE(order.skips.empty());
}

TEST(RoutePackOrder, ReadsSkipLinesApartFromTheOrder)
{
    Bots::PackOrderResult const order = Bots::ParsePackOrder(
        "pack 48648\n"
        "skip 48700 # behind the low wall\n"
        "pack 48653\n"
        "skip 48701\n");
    ASSERT_TRUE(order.ok) << order.error;
    EXPECT_EQ(order.guids, (std::vector<std::uint32_t>{48648, 48653}));
    EXPECT_EQ(order.skips, (std::vector<std::uint32_t>{48700, 48701}));
}

TEST(RoutePackOrder, ABadLineOrATwiceNamedGuidIsAnErrorWithItsLine)
{
    EXPECT_EQ(Bots::ParsePackOrder("pack 1\nmob 2\n").error, "line 2: use: pack <spawn guid>, or skip <spawn guid>");
    EXPECT_EQ(Bots::ParsePackOrder("pack x\n").error, "line 1: use: pack <spawn guid>, or skip <spawn guid>");
    EXPECT_EQ(Bots::ParsePackOrder("pack 1 2\n").error, "line 1: use: pack <spawn guid>, or skip <spawn guid>");
    EXPECT_EQ(Bots::ParsePackOrder("skip\n").error, "line 1: use: pack <spawn guid>, or skip <spawn guid>");
    EXPECT_EQ(Bots::ParsePackOrder("pack 1\npack 1\n").error, "line 2: the guid 1 is already in the order");
    EXPECT_EQ(Bots::ParsePackOrder("pack 1\nskip 1\n").error, "line 2: the guid 1 is already in the order");
    EXPECT_FALSE(Bots::ParsePackOrder("pack 1\npack 1\n").ok);
}

namespace
{
    // The first mob of each pack, in the order of the route.
    std::vector<std::uint32_t> FoughtGuids(Bots::Route const& route)
    {
        std::vector<std::uint32_t> guids;
        for (Bots::RouteStep const& step : route.steps)
            if (step.kind == Bots::RouteStepKind::Pack)
                guids.push_back(route.packs[step.index].mobs[0].spawnGuid);
        return guids;
    }
}

TEST(RouteGenerator, TheHandOrderPutsItsPacksWhereTheFirstOfThemWas)
{
    // Without an order: 2, 1, then 3 on a quest detour, then the bosses.
    FakeNav nav = Dungeon();
    Bots::GeneratorInput input = DungeonInput(1);
    ASSERT_EQ(FoughtGuids(Bots::GenerateRoute(input, nav).route), (std::vector<std::uint32_t>{2, 1, 3, 5, 6}));

    input.packOrder = {1, 2};
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;
    EXPECT_EQ(FoughtGuids(generated.route), (std::vector<std::uint32_t>{1, 2, 3, 5, 6}));
    // The names follow the new order.
    EXPECT_EQ(Bots::FindPack(generated.route, "pack1")->mobs[0].spawnGuid, 1u);
}

TEST(RouteGenerator, AHandOrderedPackKeepsItsPullPointAndTheRouteWalksToIt)
{
    // The quest pack up the side corridor, 3, comes before the line pack 1.
    // A door at the middle of the room joins the corridor to the side corridor.
    FakeNav nav = Dungeon();
    nav.doors.push_back({120, 0, 0});
    Bots::GeneratorInput input = DungeonInput(1);
    input.packOrder = {3, 1};
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;
    Bots::Route const& route = generated.route;

    EXPECT_EQ(FoughtGuids(route), (std::vector<std::uint32_t>{2, 3, 1, 5, 6}));
    for (std::size_t i = 0; i < route.steps.size(); ++i)
    {
        if (route.steps[i].kind != Bots::RouteStepKind::Pack)
            continue;
        Bots::RoutePack const& pack = route.packs[route.steps[i].index];
        ASSERT_EQ(route.steps[i - 1].kind, Bots::RouteStepKind::Point);
        Bots::RoutePoint const& pull = route.points[route.steps[i - 1].index];
        EXPECT_EQ(pull.name, "pull_" + pack.name);
        if (pack.mobs[0].spawnGuid == 3)
        {
            // In the hand order, the pack is part of the route, not of the quest.
            EXPECT_EQ(pack.quest, 0u);
            EXPECT_EQ(pull.quest, 0u);
            EXPECT_GT(pull.places[0].position.y, 15.0f) << "the pull point stays in the side corridor";
        }
    }
    // The route walks up the side corridor to the pull point of 3: the points
    // on the way are at most a point spacing and a half apart.
    std::vector<Position> walk;
    for (Bots::RouteStep const& step : route.steps)
        if (step.kind == Bots::RouteStepKind::Point)
            walk.push_back(route.points[step.index].places[0].position);
    for (std::size_t i = 1; i < walk.size(); ++i)
        EXPECT_LE(Distance2d(walk[i - 1], walk[i]), 1.5f * input.settings.pointSpacing + 2.0f * input.settings.sideLimit)
            << "from point " << i - 1;
    EXPECT_TRUE(Bots::CheckRouteWalk(route, nav).empty());
    // No quest point of the detour is left, as it has no pack left.
    for (Bots::RoutePoint const& point : route.points)
        EXPECT_NE(point.quest, 5723u) << point.name;
}

TEST(RagefireChasmRoute, TheCommittedRouteFightsTheHandOrderedPacksInTheirOrder)
{
    auto read = [](std::string const& name) {
        std::ifstream file(std::string(BOT_DATA_DIR) + "/routes/" + name);
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    };
    Bots::PackOrderResult const order = Bots::ParsePackOrder(read("ragefire_chasm.order.txt"));
    ASSERT_TRUE(order.ok) << order.error;
    ASSERT_FALSE(order.guids.empty());
    Bots::RouteResult const result = Bots::ParseRoute(read("ragefire_chasm.txt"));
    ASSERT_TRUE(result.ok) << result.error;

    // The pack step of each named guid, in the order of the file.
    std::vector<std::size_t> stepOf;
    for (std::uint32_t guid : order.guids)
    {
        std::size_t found = result.route.steps.size();
        for (std::size_t i = 0; i < result.route.steps.size(); ++i)
        {
            Bots::RouteStep const& step = result.route.steps[i];
            if (step.kind != Bots::RouteStepKind::Pack)
                continue;
            for (Bots::RouteMob const& mob : result.route.packs[step.index].mobs)
                if (mob.spawnGuid == guid)
                    found = i;
        }
        ASSERT_LT(found, result.route.steps.size()) << "no pack of the route has the spawn " << guid;
        stepOf.push_back(found);
    }
    for (std::size_t k = 1; k < stepOf.size(); ++k)
        EXPECT_LT(stepOf[k - 1], stepOf[k]) << "spawn " << order.guids[k] << " comes too early";
}

TEST(RouteGenerator, AHandOrderedPackGetsAPullPointThatDoesNotPullAPackOfLater)
{
    // A big room. Pack 1 stands by the line, pack 2 off it to the south:
    // the line reaches pack 2 right next to pack 1. The hand order fights
    // pack 2 first, so its pull point must keep out of the aggro range of
    // pack 1, which still lives. Pack 2 wanders, so its aggro range reaches
    // farther from its spawn.
    FakeNav nav;
    nav.rooms = {{0, 200, -40, 40}};
    Bots::GeneratorInput input;
    input.name = "test_room";
    input.mapId = 389;
    input.entrance = {2, 0, 0};
    input.spawns = {Spawn(1, 11318, 100, 5), Spawn(2, 11318, 110, -22, 5.0f), Spawn(5, 100, 195, 0)};
    input.bosses = {100};
    ASSERT_EQ(FoughtGuids(Bots::GenerateRoute(input, nav).route), (std::vector<std::uint32_t>{1, 2, 5}));

    input.packOrder = {2, 1};
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;
    Bots::Route const& route = generated.route;
    ASSERT_EQ(FoughtGuids(route), (std::vector<std::uint32_t>{2, 1, 5}));
    Bots::RoutePoint const* pull = nullptr;
    for (Bots::RoutePoint const& point : route.points)
        if (point.name == "pull_pack1")
            pull = &point;
    ASSERT_NE(pull, nullptr);
    for (Bots::RoutePlace const& place : pull->places)
    {
        EXPECT_GE(Distance2d(place.position, {100, 5, 0}), input.settings.aggroRange) << "pack 1 joins the pull";
        EXPECT_LE(Distance2d(place.position, {110, -22, 0}), input.settings.aggroRange + input.settings.margin + 5.0f)
            << "the pull point is near its pack";
    }
}

TEST(RouteGenerator, ASecondRegenWithTheHandOrderGivesTheSameRoute)
{
    FakeNav nav = Dungeon();
    nav.doors.push_back({120, 0, 0});
    Bots::GeneratorInput input = DungeonInput(1);
    input.packOrder = {3, 1};
    Bots::GeneratedRoute const first = Bots::GenerateRoute(input, nav);
    Bots::GeneratedRoute const second = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(first.ok) << first.error;
    ASSERT_TRUE(second.ok) << second.error;
    EXPECT_EQ(FoughtGuids(first.route), FoughtGuids(second.route));
    ASSERT_EQ(first.route.points.size(), second.route.points.size());
    for (std::size_t i = 0; i < first.route.points.size(); ++i)
    {
        EXPECT_EQ(first.route.points[i].name, second.route.points[i].name);
        EXPECT_EQ(Distance2d(first.route.points[i].places[0].position, second.route.points[i].places[0].position), 0.0f)
            << first.route.points[i].name;
    }
}

TEST(RouteGenerator, AHandOrderGuidOfAPackThatNoPathReachesIsAWarning)
{
    // Spawn 4 stands off the navmesh: the order cannot add it.
    FakeNav nav = Dungeon();
    Bots::GeneratorInput input = DungeonInput(1);
    input.packOrder = {4, 1, 2, 77};
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;

    EXPECT_EQ(FoughtGuids(generated.route), (std::vector<std::uint32_t>{1, 2, 3, 5, 6}));
    auto warned = [&generated](std::string const& text) {
        return std::find(generated.warnings.begin(), generated.warnings.end(), text) != generated.warnings.end();
    };
    EXPECT_TRUE(warned("the hand order adds the pack of spawn 4, but no navmesh path from the step before reaches "
                       "it: it is off the route"));
    EXPECT_TRUE(warned("the hand order names spawn 77, which is in no pack"));
}

namespace
{
    bool HasWarning(std::vector<std::string> const& warnings, std::string const& start)
    {
        return std::any_of(warnings.begin(), warnings.end(),
            [&start](std::string const& w) { return w.compare(0, start.size(), start) == 0; });
    }

    // A big open room with the line along y 0 from the entrance to the boss.
    // Pack 1 stands by the line; pack 2 stands 35 yards off it, out of reach
    // of the line; pack 3 stands by the line further on.
    Bots::GeneratorInput OpenRoomInput()
    {
        Bots::GeneratorInput input;
        input.name = "test_room";
        input.mapId = 389;
        input.entrance = {2, 0, 0};
        input.spawns = {Spawn(1, 11318, 60, 10), Spawn(2, 11318, 100, 35), Spawn(3, 11319, 140, 10),
            Spawn(5, 100, 195, 0)};
        input.bosses = {100};
        return input;
    }

    FakeNav OpenRoom()
    {
        FakeNav nav;
        nav.rooms = {{0, 200, -60, 60}};
        return nav;
    }
}

TEST(RouteGenerator, ASkippedPackLeavesTheRouteAndAQuestDoesNotTakeItBack)
{
    FakeNav nav = Dungeon();
    Bots::GeneratorInput input = DungeonInput(1);
    input.skippedPacks = {1};
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;

    // Spawn 1 is a trogg of the kill count of quest 5723, but skipped.
    EXPECT_EQ(FoughtGuids(generated.route), (std::vector<std::uint32_t>{2, 3, 5, 6}));
    EXPECT_TRUE(HasWarning(generated.warnings, "quest 5723: the dungeon has only 1 of 2"));
}

TEST(RouteGenerator, AnAddedPackJoinsTheRouteAtItsPlaceInTheOrderWithAPullPoint)
{
    FakeNav nav = OpenRoom();
    Bots::GeneratorInput input = OpenRoomInput();
    ASSERT_EQ(FoughtGuids(Bots::GenerateRoute(input, nav).route), (std::vector<std::uint32_t>{1, 3, 5}));

    input.packOrder = {1, 2, 3};
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;
    Bots::Route const& route = generated.route;
    EXPECT_EQ(FoughtGuids(route), (std::vector<std::uint32_t>{1, 2, 3, 5}));

    // Its pull point comes right before it, out of its aggro range and
    // within the pull range.
    std::size_t step = 0;
    while (route.steps[step].kind != Bots::RouteStepKind::Pack || route.packs[route.steps[step].index].mobs[0].spawnGuid != 2)
        ++step;
    ASSERT_EQ(route.steps[step - 1].kind, Bots::RouteStepKind::Point);
    Bots::RoutePoint const& pull = route.points[route.steps[step - 1].index];
    EXPECT_EQ(pull.name, "pull_pack2");
    EXPECT_EQ(pull.quest, 0u);
    for (Bots::RoutePlace const& place : pull.places)
    {
        EXPECT_GE(Distance2d(place.position, {100, 35, 0}), input.settings.aggroRange);
        EXPECT_LE(Distance2d(place.position, {100, 35, 0}), input.settings.aggroRange + input.settings.margin + 1.0f);
    }
    // The route walks there and back: no two points are far apart.
    std::vector<Position> walk;
    for (Bots::RouteStep const& s : route.steps)
        if (s.kind == Bots::RouteStepKind::Point)
            walk.push_back(route.points[s.index].places[0].position);
    for (std::size_t i = 1; i < walk.size(); ++i)
        EXPECT_LE(Distance2d(walk[i - 1], walk[i]), 1.5f * input.settings.pointSpacing + 2.0f * input.settings.sideLimit)
            << "from point " << i - 1;
    EXPECT_TRUE(Bots::CheckRouteWalk(route, nav).empty());
}

TEST(RouteGenerator, AnOrderOfOnlyAddedPacksGoesWhereTheLineIsNearestByPath)
{
    FakeNav nav = OpenRoom();
    Bots::GeneratorInput input = OpenRoomInput();
    input.packOrder = {2};
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;
    // The line is nearest to pack 2 at x 100, between packs 1 and 3.
    EXPECT_EQ(FoughtGuids(generated.route), (std::vector<std::uint32_t>{1, 2, 3, 5}));
}

TEST(RouteGenerator, ARouteWithinAggroRangeOfASkippedPackIsAWarning)
{
    FakeNav nav = OpenRoom();
    Bots::GeneratorInput input = OpenRoomInput();
    input.skippedPacks = {1, 2};
    Bots::GeneratedRoute generated = Bots::GenerateRoute(input, nav);
    ASSERT_TRUE(generated.ok) << generated.error;

    EXPECT_EQ(FoughtGuids(generated.route), (std::vector<std::uint32_t>{3, 5}));
    // The line passes pack 1 at about 10 yards, and pack 2 at about 35.
    EXPECT_TRUE(HasWarning(generated.warnings, "the route passes the skipped pack of spawn 1 at "));
    EXPECT_FALSE(HasWarning(generated.warnings, "the route passes the skipped pack of spawn 2 "));
}
