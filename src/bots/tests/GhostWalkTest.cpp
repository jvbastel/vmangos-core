#include "GhostWalk.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

using Bots::GhostStep;
using Bots::NextGhostStep;
using Bots::Position;
using Bots::Route;

namespace
{
    // A ghost route along the x axis: the graveyard at x = 0, the portal at
    // x = 60.
    Route Line()
    {
        Bots::RouteResult const result = Bots::ParseRoute(
            "route ghost_389 1\n"
            "point g1 0 0 10 2\n"
            "point g2 20 0 10 2\n"
            "point g3 40 0 10 2\n"
            "point portal 60 0 10 2\n");
        EXPECT_TRUE(result.ok) << result.error;
        return result.route;
    }

    void ExpectGoesTo(GhostStep const& step, float x)
    {
        EXPECT_FALSE(step.atPortal);
        EXPECT_FLOAT_EQ(step.next.x, x);
        EXPECT_FLOAT_EQ(step.next.y, 0.0f);
    }
}

TEST(GhostWalk, TheGhostRouteOfADungeonIsNamedByItsMap)
{
    EXPECT_EQ(Bots::GhostRouteName(389), "ghost_389");
    EXPECT_TRUE(Bots::IsValidRouteName(Bots::GhostRouteName(389)));
}

TEST(GhostWalk, AGhostRouteNameGivesTheMapOfItsDungeon)
{
    std::uint32_t map = 0;
    EXPECT_TRUE(Bots::IsGhostRouteName("ghost_389", map));
    EXPECT_EQ(map, 389u);
    EXPECT_FALSE(Bots::IsGhostRouteName("ragefire_chasm", map));
    EXPECT_FALSE(Bots::IsGhostRouteName("ghost_", map));
    EXPECT_FALSE(Bots::IsGhostRouteName("ghost_38x", map));
}

TEST(GhostWalk, AGhostRouteFromANavmeshPathHasAPointAtMostEverySpacing)
{
    // A path of 50 yards along x, then 10 yards along y.
    std::vector<Position> const path = {{0, 0, 1}, {3, 0, 1}, {50, 0, 1}, {50, 10, 2}};

    Route const route = Bots::MakeGhostRoute(389, 1, path, 20.0f);

    EXPECT_EQ(route.name, "ghost_389");
    EXPECT_EQ(route.mapId, 1u);
    std::vector<float> xs, ys;
    for (Bots::RoutePoint const& point : route.points)
    {
        xs.push_back(point.places.front().position.x);
        ys.push_back(point.places.front().position.y);
    }
    EXPECT_EQ(xs, (std::vector<float>{0, 20, 40, 50, 50}));
    EXPECT_EQ(ys, (std::vector<float>{0, 0, 0, 0, 10}));
    EXPECT_EQ(route.steps.size(), route.points.size());
    // The file of the route reads back to the same points.
    Bots::RouteResult const back = Bots::ParseRoute(Bots::WriteRoute(route, Bots::RouteNames{}));
    ASSERT_TRUE(back.ok) << back.error;
    EXPECT_EQ(back.route.points.size(), route.points.size());
}

TEST(GhostWalk, AtAPointTheGhostGoesToTheNextOne)
{
    ExpectGoesTo(NextGhostStep(Line(), {0.5f, 1.0f, 10.0f}), 20.0f);
    ExpectGoesTo(NextGhostStep(Line(), {39.0f, 0.0f, 10.0f}), 60.0f);
}

TEST(GhostWalk, AwayFromTheRouteTheGhostGoesToTheNearestPoint)
{
    ExpectGoesTo(NextGhostStep(Line(), {-30.0f, 5.0f, 10.0f}), 0.0f);
    ExpectGoesTo(NextGhostStep(Line(), {17.0f, 8.0f, 10.0f}), 20.0f);
}

TEST(GhostWalk, BetweenTwoPointsTheGhostKeepsGoingForward)
{
    ExpectGoesTo(NextGhostStep(Line(), {31.0f, 0.0f, 10.0f}), 40.0f);
}

TEST(GhostWalk, AtTheLastPointTheGhostIsAtThePortal)
{
    EXPECT_TRUE(NextGhostStep(Line(), {59.0f, 1.0f, 10.0f}).atPortal);
}

// The route of #72: from the graveyard that a ghost from Ragefire Chasm
// goes to, to the Ragefire Chasm portal in Orgrimmar (area trigger 2230).
TEST(GhostWalk, TheRagefireChasmGhostRouteEndsAtThePortalInOrgrimmar)
{
    std::ifstream in(std::string(BOT_DATA_DIR) + "/routes/ghost_389.txt");
    ASSERT_TRUE(in) << "bots/data/routes/ghost_389.txt";
    std::stringstream text;
    text << in.rdbuf();
    Bots::RouteResult const result = Bots::ParseRoute(text.str());
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.route.mapId, 1u);
    ASSERT_GE(result.route.points.size(), 2u);
    Position const end = result.route.points.back().places.front().position;
    EXPECT_NEAR(end.x, 1818.4f, 12.0f);
    EXPECT_NEAR(end.y, -4427.26f, 12.0f);
}
