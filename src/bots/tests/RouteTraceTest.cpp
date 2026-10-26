#include "BotGeometry.h"
#include "RouteLine.h"
#include "RouteTrace.h"

#include <gtest/gtest.h>

#include <algorithm>

using Bots::Position;
using Bots::Trace;
using Bots::TraceMotion;
using Bots::TraceRecorder;
using Bots::TraceSettings;
using Bots::TraceState;

namespace
{
    Trace NewTrace()
    {
        Trace trace;
        trace.name = "ragefire_chasm";
        trace.mapId = 389;
        return trace;
    }

    // The GM walks from `from` to `to` along x, looking each yard.
    void WalkX(TraceRecorder& recorder, float from, float to, float y = 0.0f)
    {
        float const step = from < to ? 1.0f : -1.0f;
        for (float x = from; step > 0 ? x <= to : x >= to; x += step)
            recorder.Look({x, y, 0.0f}, TraceMotion::Ground);
    }
}

TEST(RouteTrace, RecordsAPointEachStepThatTheGmWalks)
{
    TraceRecorder recorder(NewTrace(), TraceSettings{});
    WalkX(recorder, 0.0f, 20.0f);

    Trace const& trace = recorder.GetTrace();
    ASSERT_EQ(trace.points.size(), 6u); // 0, 4, 8, 12, 16, 20
    EXPECT_FLOAT_EQ(trace.points[1].position.x, 4.0f);
    EXPECT_FLOAT_EQ(trace.points[5].position.x, 20.0f);
    EXPECT_FALSE(trace.points[0].joined);
    EXPECT_TRUE(trace.points[1].joined);
    EXPECT_FLOAT_EQ(Bots::TraceLength(trace), 20.0f);
    EXPECT_EQ(Bots::TraceStatusLine(trace, recorder.State()),
        "[route] trace on ragefire_chasm points=6 marks=0 length=20y");
}

TEST(RouteTrace, RecordsAPointWhenTheFloorChanges)
{
    TraceRecorder recorder(NewTrace(), TraceSettings{});
    recorder.Look({0.0f, 0.0f, 0.0f}, TraceMotion::Ground);
    recorder.Look({1.0f, 0.0f, 3.5f}, TraceMotion::Ground); // A ramp: 3.6 yards, but 3.5 up.

    ASSERT_EQ(recorder.GetTrace().points.size(), 2u);
    EXPECT_FLOAT_EQ(recorder.GetTrace().points[1].position.z, 3.5f);
}

TEST(RouteTrace, LeavesOutATeleportAndWarns)
{
    TraceRecorder recorder(NewTrace(), TraceSettings{});
    WalkX(recorder, 0.0f, 8.0f);
    Bots::TraceReply const reply = recorder.Look({100.0f, 0.0f, 0.0f}, TraceMotion::Ground);
    WalkX(recorder, 101.0f, 104.0f);

    EXPECT_TRUE(reply.ok);
    EXPECT_NE(reply.text.find("teleport"), std::string::npos) << reply.text;
    Trace const& trace = recorder.GetTrace();
    ASSERT_EQ(trace.points.size(), 5u); // 0 4 8, then 100 104
    EXPECT_FLOAT_EQ(trace.points[3].position.x, 100.0f);
    EXPECT_FALSE(trace.points[3].joined);
    EXPECT_TRUE(trace.points[4].joined);
    EXPECT_FLOAT_EQ(Bots::TraceLength(trace), 12.0f);
}

TEST(RouteTrace, LeavesOutAFlightAndWarnsOnce)
{
    TraceRecorder recorder(NewTrace(), TraceSettings{});
    WalkX(recorder, 0.0f, 4.0f);
    Bots::TraceReply const first = recorder.Look({8.0f, 0.0f, 5.0f}, TraceMotion::Flying);
    Bots::TraceReply const second = recorder.Look({12.0f, 0.0f, 5.0f}, TraceMotion::Flying);
    recorder.Look({16.0f, 0.0f, 0.0f}, TraceMotion::Ground);

    EXPECT_NE(first.text.find("fly"), std::string::npos) << first.text;
    EXPECT_TRUE(second.text.empty());
    Trace const& trace = recorder.GetTrace();
    ASSERT_EQ(trace.points.size(), 3u);
    EXPECT_FLOAT_EQ(trace.points[2].position.x, 16.0f);
    EXPECT_FALSE(trace.points[2].joined);
}

TEST(RouteTrace, AMarkIsACheckpointAtThePositionOfTheGm)
{
    TraceRecorder recorder(NewTrace(), TraceSettings{});
    WalkX(recorder, 0.0f, 6.0f);
    EXPECT_TRUE(recorder.Mark({6.0f, 0.0f, 0.0f}, "corner # by the lava").ok);

    Trace const& trace = recorder.GetTrace();
    ASSERT_EQ(trace.points.size(), 3u);
    EXPECT_TRUE(trace.points[2].mark);
    EXPECT_FLOAT_EQ(trace.points[2].position.x, 6.0f);
    EXPECT_EQ(trace.points[2].note, "corner by the lava");
    EXPECT_EQ(Bots::MarkCount(trace), 1u);
}

TEST(RouteTrace, APauseAddsNoPointsAndAResumeDoesNotJoinAFarPausePosition)
{
    TraceRecorder recorder(NewTrace(), TraceSettings{});
    WalkX(recorder, 0.0f, 8.0f);
    EXPECT_TRUE(recorder.Pause().ok);
    EXPECT_EQ(recorder.State(), TraceState::Paused);
    EXPECT_FALSE(recorder.Mark({8.0f, 0.0f, 0.0f}, "").ok);
    WalkX(recorder, 8.0f, 30.0f, 5.0f); // A walk back, or a break.
    EXPECT_EQ(recorder.GetTrace().points.size(), 3u);
    EXPECT_NE(Bots::TraceStatusLine(recorder.GetTrace(), recorder.State()).find("trace paused"), std::string::npos);

    Bots::TraceReply const reply = recorder.Resume({30.0f, 5.0f, 0.0f});
    EXPECT_TRUE(reply.ok);
    EXPECT_NE(reply.text.find("gap"), std::string::npos) << reply.text;
    WalkX(recorder, 31.0f, 34.0f, 5.0f);

    Trace const& trace = recorder.GetTrace();
    ASSERT_EQ(trace.points.size(), 5u);
    EXPECT_FLOAT_EQ(trace.points[3].position.x, 30.0f);
    EXPECT_FALSE(trace.points[3].joined);
    EXPECT_TRUE(trace.points[4].joined);
    EXPECT_FLOAT_EQ(Bots::TraceLength(trace), 12.0f); // 8, then 4: not the 22 yard gap.
}

TEST(RouteTrace, AResumeNearThePauseJoinsIt)
{
    TraceRecorder recorder(NewTrace(), TraceSettings{});
    WalkX(recorder, 0.0f, 8.0f);
    recorder.Pause();
    Bots::TraceReply const reply = recorder.Resume({9.0f, 0.0f, 0.0f});

    EXPECT_TRUE(reply.text.empty());
    ASSERT_EQ(recorder.GetTrace().points.size(), 4u);
    EXPECT_TRUE(recorder.GetTrace().points[3].joined);
}

TEST(RouteTrace, UndoRemovesTheEndWithItsMarksAndWaitsForTheGmAtTheNewEnd)
{
    TraceRecorder recorder(NewTrace(), TraceSettings{});
    WalkX(recorder, 0.0f, 12.0f);
    recorder.Mark({12.0f, 0.0f, 0.0f}, "wrong turn");
    WalkX(recorder, 12.0f, 24.0f);
    ASSERT_EQ(recorder.GetTrace().points.size(), 8u); // 0 4 8 12 mark12 16 20 24

    Bots::TraceReply const reply = recorder.Undo(12.0f); // 24 20 16, and 12 with its mark
    EXPECT_TRUE(reply.ok);
    Trace const& trace = recorder.GetTrace();
    ASSERT_EQ(trace.points.size(), 3u);
    EXPECT_FLOAT_EQ(trace.points.back().position.x, 8.0f);
    EXPECT_EQ(Bots::MarkCount(trace), 0u);

    // The GM is 16 yards away: no point joins that spot with a straight line.
    WalkX(recorder, 24.0f, 13.0f);
    EXPECT_EQ(trace.points.size(), 3u);
    // Back at the end, the trace goes on.
    WalkX(recorder, 12.0f, 12.0f);
    WalkX(recorder, 12.0f, 12.0f, 3.0f);
    ASSERT_EQ(trace.points.size(), 4u);
    EXPECT_FLOAT_EQ(trace.points.back().position.y, 3.0f);
    EXPECT_TRUE(trace.points.back().joined);
}

TEST(RouteTrace, ASecondStartAddsALaterWalkAtTheEnd)
{
    TraceRecorder first(NewTrace(), TraceSettings{});
    WalkX(first, 0.0f, 8.0f);

    TraceRecorder second(first.GetTrace(), TraceSettings{});
    WalkX(second, 4.0f, 4.0f, 3.0f);
    WalkX(second, 4.0f, 4.0f, 7.0f);

    Trace const& trace = second.GetTrace();
    ASSERT_EQ(trace.points.size(), 5u);
    EXPECT_TRUE(trace.points[3].walk);
    EXPECT_FALSE(trace.points[3].joined);
    EXPECT_TRUE(trace.points[4].joined);
    EXPECT_FALSE(trace.points[4].walk);
}

TEST(RouteTrace, TheTraceFileReadsBackToTheSameTrace)
{
    TraceRecorder recorder(NewTrace(), TraceSettings{});
    WalkX(recorder, 0.0f, 8.0f);
    recorder.Mark({8.5f, 0.25f, -1.0f}, "the ramp");
    recorder.Pause();
    recorder.Resume({40.0f, 0.0f, 0.0f});
    TraceRecorder later(recorder.GetTrace(), TraceSettings{});
    later.Look({2.0f, 2.0f, 0.0f}, TraceMotion::Ground);

    std::string const text = Bots::WriteTrace(later.GetTrace());
    Bots::TraceResult const parsed = Bots::ParseTrace(text);
    ASSERT_TRUE(parsed.ok) << parsed.error;
    EXPECT_EQ(Bots::WriteTrace(parsed.trace), text);
    ASSERT_EQ(parsed.trace.points.size(), 6u);
    EXPECT_EQ(parsed.trace.name, "ragefire_chasm");
    EXPECT_EQ(parsed.trace.mapId, 389u);
    EXPECT_TRUE(parsed.trace.points[3].mark);
    EXPECT_EQ(parsed.trace.points[3].note, "the ramp");
    EXPECT_FALSE(parsed.trace.points[4].joined);
    EXPECT_FALSE(parsed.trace.points[4].walk);
    EXPECT_TRUE(parsed.trace.points[5].walk);
}

TEST(RouteTrace, ABadTraceFileNamesTheLine)
{
    Bots::TraceResult const parsed = Bots::ParseTrace("trace rfc 389\np 1 2\n");
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.error.rfind("line 2:", 0), 0u) << parsed.error;
}

// .route build: the route around a trace.
namespace
{
    using Bots::GeneratorSpawn;
    using Bots::Route;
    using Bots::RouteStepKind;
    using Bots::TraceBuild;
    using Bots::TraceBuildInput;

    GeneratorSpawn Mob(std::uint32_t guid, std::uint32_t entry, float x, float y)
    {
        return GeneratorSpawn{guid, entry, {x, y, 0.0f}};
    }

    // A walk along y = `y` from x `from` to `to`, a point every 4 yards.
    void AddWalk(Trace& trace, float from, float to, float y = 0.0f, bool joined = true)
    {
        float const step = from < to ? 4.0f : -4.0f;
        for (float x = from; step > 0 ? x <= to : x >= to; x += step)
        {
            Bots::TracePoint point;
            point.position = {x, y, 0.0f};
            point.joined = joined && !trace.points.empty();
            joined = true;
            trace.points.push_back(point);
        }
    }

    TraceBuildInput Corridor()
    {
        TraceBuildInput input;
        input.trace = NewTrace();
        AddWalk(input.trace, 0.0f, 100.0f);
        input.entryName = [](std::uint32_t entry) { return entry == 900 ? std::string("Oggleflint") : std::string(); };
        return input;
    }

    // The names of the steps in order.
    std::vector<std::string> StepNames(Route const& route)
    {
        std::vector<std::string> names;
        for (Bots::RouteStep const& step : route.steps)
            names.push_back(step.kind == RouteStepKind::Point ? route.points[step.index].name
                                                              : route.packs[step.index].name);
        return names;
    }

    std::vector<Bots::RoutePoint const*> PointsOf(Route const& route)
    {
        std::vector<Bots::RoutePoint const*> points;
        for (Bots::RouteStep const& step : route.steps)
            if (step.kind == RouteStepKind::Point)
                points.push_back(&route.points[step.index]);
        return points;
    }

    // The distance of a position from the joined points of the trace.
    float OffTrace(Trace const& trace, Position const& p)
    {
        float best = 1e9f;
        for (std::size_t i = 0; i < trace.points.size(); ++i)
        {
            best = std::min(best, Bots::Distance3d(p, trace.points[i].position));
            if (i > 0 && trace.points[i].joined)
                best = std::min(best, Bots::DistanceToSegment(p, trace.points[i - 1].position, trace.points[i].position));
        }
        return best;
    }
}

TEST(RouteTraceBuild, PutsRoutePointsAlongTheTraceAtTheSpacing)
{
    TraceBuildInput input = Corridor();
    input.trace.points.resize(11); // 0 to 40
    TraceBuild const built = Bots::BuildTraceRoute(input);

    ASSERT_TRUE(built.ok) << built.error;
    std::vector<Bots::RoutePoint const*> const points = PointsOf(built.route);
    ASSERT_EQ(points.size(), 5u);
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        ASSERT_EQ(points[i]->places.size(), 1u) << "no alternatives";
        EXPECT_FLOAT_EQ(points[i]->places[0].position.x, 10.0f * float(i));
        EXPECT_FLOAT_EQ(points[i]->places[0].position.y, 0.0f);
        EXPECT_FLOAT_EQ(points[i]->places[0].radius, 2.0f);
    }
    EXPECT_EQ(built.route.name, "ragefire_chasm");
    EXPECT_EQ(built.route.mapId, 389u);
    EXPECT_FALSE(built.route.hasSeed);
}

TEST(RouteTraceBuild, TakesThePacksThatTheTracePassesInTheOrderItReachesThem)
{
    TraceBuildInput input = Corridor();
    input.spawns = {
        Mob(1, 100, 70.0f, 15.0f),  // 15 yards from the trace: on the route.
        Mob(2, 101, 30.0f, -20.0f), // 20 yards: on the route, and first.
        Mob(3, 102, 50.0f, 40.0f),  // 40 yards: off the route.
        Mob(4, 100, 31.0f, -24.0f), // 4 yards from spawn 2: its pack.
    };
    TraceBuild const built = Bots::BuildTraceRoute(input);

    ASSERT_TRUE(built.ok) << built.error;
    Route const& route = built.route;
    ASSERT_EQ(route.packs.size(), 2u);
    std::vector<std::uint32_t> firstGuids;
    for (Bots::RouteMob const& mob : route.packs[0].mobs)
        firstGuids.push_back(mob.spawnGuid);
    EXPECT_EQ(firstGuids, (std::vector<std::uint32_t>{2, 4}));
    EXPECT_EQ(route.packs[1].mobs[0].spawnGuid, 1u);
    std::vector<std::string> const names = StepNames(route);
    auto at = [&names](std::string const& name) { return std::find(names.begin(), names.end(), name) - names.begin(); };
    EXPECT_LT(at("pull_pack1"), at("pack1"));
    EXPECT_LT(at("pack1"), at("pull_pack2"));
    EXPECT_LT(at("pull_pack2"), at("pack2"));
}

TEST(RouteTraceBuild, PutsEachPullPointOnTheTraceOutsideTheAggroRangeOfItsPack)
{
    TraceBuildInput input = Corridor();
    input.spawns = {Mob(2, 101, 30.0f, -20.0f)};
    TraceBuild const built = Bots::BuildTraceRoute(input);

    ASSERT_TRUE(built.ok) << built.error;
    Bots::RoutePoint const* pull = Bots::FindPoint(built.route, "pull_pack1");
    ASSERT_NE(pull, nullptr);
    Position const at = pull->places[0].position;
    // The trace first comes within 20 + 5 yards of the pack at x 15.
    EXPECT_NEAR(at.x, 15.0f, 1.0f);
    EXPECT_FLOAT_EQ(at.y, 0.0f);
    EXPECT_GE(Bots::Distance3d(at, {30.0f, -20.0f, 0.0f}), 20.0f);
    for (Bots::RoutePoint const* point : PointsOf(built.route))
        EXPECT_LT(OffTrace(input.trace, point->places[0].position), 0.01f) << point->name;
}

TEST(RouteTraceBuild, KeepsEachCheckpointAsARoutePointAtItsExactPosition)
{
    TraceBuildInput input = Corridor();
    input.trace.points.resize(11); // 0 to 40
    Bots::TracePoint mark;
    mark.position = {13.37f, 0.0f, 0.0f};
    mark.mark = true;
    input.trace.points.insert(input.trace.points.begin() + 4, mark); // after 12
    TraceBuild const built = Bots::BuildTraceRoute(input);

    ASSERT_TRUE(built.ok) << built.error;
    std::vector<float> xs;
    for (Bots::RoutePoint const* point : PointsOf(built.route))
        xs.push_back(point->places[0].position.x);
    // 10 is too near the checkpoint, and the spacing counts again from it.
    std::vector<float> const expected{0.0f, 13.37f, 23.37f, 33.37f, 40.0f};
    ASSERT_EQ(xs.size(), expected.size());
    for (std::size_t i = 0; i < xs.size(); ++i)
        EXPECT_NEAR(xs[i], expected[i], 0.01f) << i;
    Bots::RoutePoint const* checkpoint = Bots::FindPoint(built.route, "mark1");
    ASSERT_NE(checkpoint, nullptr);
    EXPECT_FLOAT_EQ(checkpoint->places[0].position.x, 13.37f);
}

TEST(RouteTraceBuild, PutsNoRoutePointOnAGapOfTheTrace)
{
    TraceBuildInput input;
    input.trace = NewTrace();
    AddWalk(input.trace, 0.0f, 20.0f);
    AddWalk(input.trace, 60.0f, 80.0f, 0.0f, false);
    TraceBuild const built = Bots::BuildTraceRoute(input);

    ASSERT_TRUE(built.ok) << built.error;
    std::vector<float> xs;
    for (Bots::RoutePoint const* point : PointsOf(built.route))
        xs.push_back(point->places[0].position.x);
    EXPECT_EQ(xs, (std::vector<float>{0.0f, 10.0f, 20.0f, 60.0f, 70.0f, 80.0f}));
}

TEST(RouteTraceBuild, WalksALaterWalkThatStartsOnTheTraceOutAndBackThere)
{
    TraceBuildInput input = Corridor();
    Bots::TracePoint start;
    start.position = {52.0f, 2.0f, 0.0f};
    start.joined = false;
    start.walk = true;
    input.trace.points.push_back(start);
    for (float y = 6.0f; y <= 40.0f; y += 4.0f)
        input.trace.points.push_back({{52.0f, y, 0.0f}});
    input.spawns = {Mob(1, 100, 52.0f, 62.0f)}; // Only the later walk comes near.
    TraceBuild const built = Bots::BuildTraceRoute(input);

    ASSERT_TRUE(built.ok) << built.error;
    std::vector<std::string> const names = StepNames(built.route);
    auto pack = std::find(names.begin(), names.end(), "pack1");
    ASSERT_NE(pack, names.end());
    // Before the pack: no point past x 55 on the corridor. After it: the
    // walk back, then the rest of the corridor.
    bool backOnCorridor = false;
    for (auto it = names.begin(); it != names.end(); ++it)
    {
        Bots::RoutePoint const* point = Bots::FindPoint(built.route, *it);
        if (!point)
            continue;
        Position const p = point->places[0].position;
        if (it < pack)
            EXPECT_LE(p.x, 55.0f) << *it;
        else if (p.x > 55.0f)
            backOnCorridor = true;
    }
    EXPECT_TRUE(backOnCorridor);
    Position const last = PointsOf(built.route).back()->places[0].position;
    EXPECT_FLOAT_EQ(last.x, 100.0f);
}

TEST(RouteTraceBuild, NamesTheBossPackAndReportsABossThatTheTraceDoesNotPass)
{
    TraceBuildInput input = Corridor();
    input.spawns = {Mob(1, 900, 60.0f, 10.0f), Mob(2, 901, 50.0f, 60.0f)};
    input.bosses = {900, 901};
    TraceBuild const built = Bots::BuildTraceRoute(input);

    ASSERT_TRUE(built.ok) << built.error;
    EXPECT_NE(Bots::FindPack(built.route, "oggleflint"), nullptr);
    EXPECT_EQ(built.route.bosses, (std::vector<std::uint32_t>{900, 901}));
    EXPECT_EQ(built.unmetBosses, (std::vector<std::uint32_t>{901}));
}

TEST(RouteTraceBuild, ReportsAnUnmetKillCountWithTheNearestPacksAndAddsNoPack)
{
    TraceBuildInput input = Corridor();
    input.spawns = {
        Mob(1, 100, 30.0f, 10.0f),  // On the route.
        Mob(2, 100, 50.0f, 40.0f),  // Off the route, 40 yards.
        Mob(3, 100, 54.0f, 40.0f),  // Its pack.
        Mob(4, 100, 50.0f, -90.0f), // Farther.
    };
    Bots::GeneratorQuest quest{5723, {}, {}};
    Bots::QuestObjective kill{Bots::QuestObjectiveKind::Kill};
    kill.entry = 100;
    kill.count = 3;
    quest.objectives.push_back(kill);
    input.quests = {quest};
    TraceBuild const built = Bots::BuildTraceRoute(input);

    ASSERT_TRUE(built.ok) << built.error;
    EXPECT_EQ(built.route.packs.size(), 1u);
    ASSERT_EQ(built.unmet.size(), 1u);
    Bots::UnmetObjective const& unmet = built.unmet[0];
    EXPECT_EQ(unmet.quest, 5723u);
    EXPECT_EQ(unmet.have, 1u);
    EXPECT_EQ(unmet.need, 3u);
    ASSERT_EQ(unmet.nearest.size(), 1u);
    EXPECT_EQ(unmet.nearest[0].count, 2u);
    EXPECT_EQ(unmet.nearest[0].mobs[0].spawnGuid, 2u);
    EXPECT_FLOAT_EQ(unmet.nearest[0].position.x, 50.0f);
    EXPECT_NEAR(unmet.nearest[0].distance, 40.0f, 0.5f);
    ASSERT_NE(Bots::FindQuest(built.route, 5723), nullptr);
}

TEST(RouteTraceBuild, PutsAVisitPointOnTheTraceAndReportsAVisitThatTheTraceMisses)
{
    TraceBuildInput input = Corridor();
    Bots::GeneratorQuest near{5722, {}, {{"Maur Grimtotem", {40.0f, 3.0f, 0.0f}}}};
    Bots::GeneratorQuest far{5724, {}, {{"Far corpse", {40.0f, 30.0f, 0.0f}}}};
    input.quests = {near, far};
    TraceBuild const built = Bots::BuildTraceRoute(input);

    ASSERT_TRUE(built.ok) << built.error;
    Bots::RoutePoint const* visit = Bots::FindPoint(built.route, "q5722_maur_grimtotem");
    ASSERT_NE(visit, nullptr);
    EXPECT_NEAR(visit->places[0].position.x, 40.0f, 0.5f);
    EXPECT_FLOAT_EQ(visit->places[0].position.y, 0.0f);
    ASSERT_EQ(built.unmet.size(), 1u);
    EXPECT_EQ(built.unmet[0].quest, 5724u);
    EXPECT_NEAR(built.unmet[0].distance, 30.0f, 0.5f);
}

TEST(RouteTraceBuild, FailsOnATraceWithNoPoints)
{
    TraceBuildInput input;
    input.trace = NewTrace();
    EXPECT_FALSE(Bots::BuildTraceRoute(input).ok);
}
