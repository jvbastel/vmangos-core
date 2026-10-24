#include "Route.h"

#include <gtest/gtest.h>

using Bots::ParseRoute;
using Bots::Route;
using Bots::RouteResult;
using Bots::RouteStepKind;

namespace
{
    char const* const RagefireStart =
        "# Ragefire Chasm\n"
        "route ragefire_chasm 389\n"
        "boss 11517 # Oggleflint\n"
        "boss 11520\n"
        "point entrance 3.81 -14.82 -17.84 2\n"
        "alt 5.1 -12 -17.8 2.5\n"
        "pack pack1 -3.2 10.1 -20\n"
        "mob 79123 11319 # Ragefire Shaman\n"
        "mob 79124 11321\n"
        "point hall 10 20 -30 3\n";
}

TEST(Route, ReadsPointsPacksAndBossesInFileOrder)
{
    RouteResult result = ParseRoute(RagefireStart);

    ASSERT_TRUE(result.ok) << result.error;
    Route const& route = result.route;
    EXPECT_EQ(route.name, "ragefire_chasm");
    EXPECT_EQ(route.mapId, 389u);
    EXPECT_EQ(route.bosses, (std::vector<std::uint32_t>{11517, 11520}));

    ASSERT_EQ(route.steps.size(), 3u);
    EXPECT_EQ(route.steps[0].kind, RouteStepKind::Point);
    EXPECT_EQ(route.steps[1].kind, RouteStepKind::Pack);
    EXPECT_EQ(route.steps[2].kind, RouteStepKind::Point);

    Bots::RoutePoint const& entrance = route.points[route.steps[0].index];
    EXPECT_EQ(entrance.name, "entrance");
    ASSERT_EQ(entrance.places.size(), 2u);
    EXPECT_FLOAT_EQ(entrance.places[0].position.x, 3.81f);
    EXPECT_FLOAT_EQ(entrance.places[0].position.y, -14.82f);
    EXPECT_FLOAT_EQ(entrance.places[0].position.z, -17.84f);
    EXPECT_FLOAT_EQ(entrance.places[0].radius, 2.0f);
    EXPECT_FLOAT_EQ(entrance.places[1].position.x, 5.1f);
    EXPECT_FLOAT_EQ(entrance.places[1].radius, 2.5f);

    Bots::RoutePack const& pack = route.packs[route.steps[1].index];
    EXPECT_EQ(pack.name, "pack1");
    EXPECT_FLOAT_EQ(pack.position.y, 10.1f);
    ASSERT_EQ(pack.mobs.size(), 2u);
    EXPECT_EQ(pack.mobs[0].spawnGuid, 79123u);
    EXPECT_EQ(pack.mobs[0].entry, 11319u);
    EXPECT_EQ(pack.mobs[1].spawnGuid, 79124u);

    EXPECT_EQ(route.points[route.steps[2].index].name, "hall");
}

TEST(Route, ScriptsFindPointsAndPacksByName)
{
    Route route = ParseRoute(RagefireStart).route;

    ASSERT_NE(Bots::FindPoint(route, "hall"), nullptr);
    EXPECT_FLOAT_EQ(Bots::FindPoint(route, "hall")->places[0].radius, 3.0f);
    ASSERT_NE(Bots::FindPack(route, "pack1"), nullptr);
    EXPECT_EQ(Bots::FindPack(route, "pack1")->mobs.size(), 2u);
    EXPECT_EQ(Bots::FindPoint(route, "pack1"), nullptr);
    EXPECT_EQ(Bots::FindPack(route, "nowhere"), nullptr);
}

TEST(Route, AnEmptyRouteHasOnlyItsRouteLine)
{
    RouteResult result = ParseRoute("route wailing_caverns 43\n");

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(result.route.steps.empty());
    EXPECT_TRUE(result.route.bosses.empty());
}

TEST(Route, ErrorsNameTheLine)
{
    struct Case
    {
        char const* text;
        char const* error;
    };
    Case const cases[] = {
        {"", "the file has no route line"},
        {"# only a comment\n", "the file has no route line"},
        {"boss 11517\nroute rfc 389\n", "line 1: the file must start with: route <name> <map>"},
        {"route rfc 389\nroute rfc 389\n", "line 2: the file has more than one route line"},
        {"route Ragefire 389\n", "line 1: the route name 'Ragefire' must have only a-z, 0-9, and _"},
        {"route rfc\n", "line 1: use: route <name> <map>"},
        {"route rfc 389\nboss oggleflint\n", "line 2: use: boss <entry>"},
        {"route rfc 389\nboss 11517\nboss 11517\n", "line 3: the boss 11517 is already in the boss list"},
        {"route rfc 389\npoint a 1 2 3\n", "line 2: use: point <name> <x> <y> <z> <radius>"},
        {"route rfc 389\npoint a 1 2 z 2\n", "line 2: use: point <name> <x> <y> <z> <radius>"},
        {"route rfc 389\npoint a 1 2 3 0\n", "line 2: the radius must be more than 0"},
        {"route rfc 389\npoint 12 1 2 3 2\n",
            "line 2: the name '12' must have only letters, digits, and _, and not only digits"},
        {"route rfc 389\npoint a 1 2 3 2\npack a 1 2 3\n", "line 3: the name 'a' is already in the route"},
        {"route rfc 389\nalt 1 2 3 2\n", "line 2: an alternative needs a point before it"},
        {"route rfc 389\nmob 79123 11319\n", "line 2: a mob needs a pack before it"},
        {"route rfc 389\npack a 1 2 3\nmob 79123 11319\npack b 1 2 3\nmob 79123 11319\n",
            "line 5: the mob 79123 is already in the pack a"},
        {"route rfc 389\npack a 1 2 3\nmob -5 11319\n", "line 3: use: mob <spawn guid> <entry>"},
        {"route rfc 389\nwalk 1 2 3\n", "line 2: unknown word 'walk'. Use route, boss, point, alt, pack, or mob"},
    };
    for (Case const& c : cases)
    {
        RouteResult result = ParseRoute(c.text);
        EXPECT_FALSE(result.ok) << c.text;
        EXPECT_EQ(result.error, c.error) << c.text;
    }
}

TEST(Route, WritesAFileThatReadsBackToTheSameRoute)
{
    Route route = ParseRoute(RagefireStart).route;

    std::string const text = Bots::WriteRoute(route, [](std::uint32_t entry) {
        return entry == 11319 ? std::string("Ragefire Shaman") : std::string();
    });

    EXPECT_EQ(text,
        "# The route of ragefire_chasm. The GM command .route wrote this file.\n"
        "route ragefire_chasm 389\n"
        "boss 11517\n"
        "boss 11520\n"
        "point entrance 3.81 -14.82 -17.84 2.00\n"
        "alt 5.10 -12.00 -17.80 2.50\n"
        "pack pack1 -3.20 10.10 -20.00\n"
        "mob 79123 11319 # Ragefire Shaman\n"
        "mob 79124 11321\n"
        "point hall 10.00 20.00 -30.00 3.00\n");
    RouteResult again = ParseRoute(text);
    ASSERT_TRUE(again.ok) << again.error;
    EXPECT_EQ(Bots::WriteRoute(again.route, nullptr), Bots::WriteRoute(route, nullptr));
}

TEST(RouteRecorder, NamesNewPointsAndPacksWithTheFirstFreeNumber)
{
    Route route = ParseRoute("route rfc 389\npoint p2 0 0 0 2\n").route;

    EXPECT_EQ(Bots::AddPoint(route, "", {{1, 2, 3}, 2}), "");
    EXPECT_EQ(Bots::AddPoint(route, "", {{4, 5, 6}, 2}), "");
    EXPECT_EQ(Bots::AddPack(route, "", {7, 8, 9}, {{79123, 11319}, {79124, 11321}}), "");
    EXPECT_EQ(Bots::AddPack(route, "boss_ogg", {7, 8, 9}, {{79125, 11517}}), "");

    ASSERT_EQ(route.points.size(), 3u);
    EXPECT_EQ(route.points[1].name, "p1");
    EXPECT_EQ(route.points[2].name, "p3");
    ASSERT_EQ(route.packs.size(), 2u);
    EXPECT_EQ(route.packs[0].name, "pack1");
    EXPECT_EQ(route.packs[0].mobs.size(), 2u);
    EXPECT_EQ(route.packs[1].name, "boss_ogg");
    EXPECT_EQ(route.steps.size(), 5u);
}

TEST(RouteRecorder, AlternativesAndMobsJoinTheLastPointAndPack)
{
    Route route = ParseRoute("route rfc 389\npoint a 0 0 0 2\npack b 1 1 1\npoint c 2 2 2 2\n").route;

    EXPECT_EQ(Bots::AddAlternative(route, {{3, 3, 3}, 4}), "");
    EXPECT_EQ(Bots::AddMob(route, {79123, 11319}), "");

    EXPECT_EQ(Bots::FindPoint(route, "a")->places.size(), 1u);
    ASSERT_EQ(Bots::FindPoint(route, "c")->places.size(), 2u);
    EXPECT_FLOAT_EQ(Bots::FindPoint(route, "c")->places[1].radius, 4.0f);
    EXPECT_EQ(Bots::FindPack(route, "b")->mobs.size(), 1u);
}

TEST(RouteRecorder, APackWithAMobOfAnotherPackIsNotAdded)
{
    Route route = ParseRoute("route rfc 389\npack a 0 0 0\nmob 79123 11319\n").route;

    EXPECT_EQ(Bots::AddPack(route, "", {1, 1, 1}, {{79124, 11321}, {79123, 11319}}),
        "the mob 79123 is already in the pack a");

    EXPECT_EQ(route.packs.size(), 1u);
    EXPECT_EQ(route.steps.size(), 1u);
}

TEST(RouteRecorder, UndoRemovesTheLastStepWithItsAlternativesOrMobs)
{
    Route route = ParseRoute(
        "route rfc 389\npoint a 0 0 0 2\npack b 1 1 1\nmob 1 2\npoint c 2 2 2 2\nalt 3 3 3 2\n").route;

    EXPECT_EQ(Bots::RemoveLastStep(route), "");
    EXPECT_EQ(Bots::FindPoint(route, "c"), nullptr);
    EXPECT_EQ(Bots::RemoveLastStep(route), "");
    EXPECT_EQ(Bots::FindPack(route, "b"), nullptr);
    EXPECT_EQ(Bots::AddMob(route, {1, 2}), "a mob needs a pack before it");
    EXPECT_EQ(Bots::RemoveLastStep(route), "");
    EXPECT_EQ(Bots::RemoveLastStep(route), "the route has no step");
}

TEST(RouteRecorder, RouteNamesAreSafeFileNames)
{
    EXPECT_TRUE(Bots::IsValidRouteName("ragefire_chasm"));
    EXPECT_TRUE(Bots::IsValidRouteName("rfc2"));
    EXPECT_FALSE(Bots::IsValidRouteName(""));
    EXPECT_FALSE(Bots::IsValidRouteName("../etc/passwd"));
    EXPECT_FALSE(Bots::IsValidRouteName("Ragefire"));
    EXPECT_FALSE(Bots::IsValidRouteName("rfc.txt"));
}

TEST(BossList, KnowsTheBossesOfEachDungeonByMap)
{
    Bots::BossList bosses;
    bosses.Add(ParseRoute(RagefireStart).route);
    bosses.Add(ParseRoute("route wailing_caverns 43\nboss 3653\n").route);

    EXPECT_TRUE(bosses.IsBoss(389, 11517));
    EXPECT_TRUE(bosses.IsBoss(389, 11520));
    EXPECT_TRUE(bosses.IsBoss(43, 3653));
    // A trash mob of the route, and a boss of another dungeon.
    EXPECT_FALSE(bosses.IsBoss(389, 11319));
    EXPECT_FALSE(bosses.IsBoss(43, 11517));
}

TEST(BossList, TwoRoutesOfOneDungeonAddTheirBosses)
{
    Bots::BossList bosses;
    bosses.Add(ParseRoute("route rfc_a 389\nboss 11517\n").route);
    bosses.Add(ParseRoute("route rfc_b 389\nboss 11518\n").route);

    EXPECT_TRUE(bosses.IsBoss(389, 11517));
    EXPECT_TRUE(bosses.IsBoss(389, 11518));
}
