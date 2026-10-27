#include "ScriptedMove.h"

#include <gtest/gtest.h>

using Bots::ScriptedMove;

TEST(ScriptedMove, AMoveIsInProgressFromItsStartToItsArrival)
{
    ScriptedMove move;
    EXPECT_FALSE(move.InProgress());

    EXPECT_EQ(move.Start("Ezzra", "pull_pack3"), "Ezzra moves to pull_pack3");
    EXPECT_TRUE(move.InProgress());

    EXPECT_EQ(move.Arrive(), "Ezzra arrives at pull_pack3");
    EXPECT_FALSE(move.InProgress());
    EXPECT_EQ(move.Stopped(), 0u);
}

TEST(ScriptedMove, AMoveThatStopsNamesItsReasonAndCounts)
{
    ScriptedMove move;
    move.Start("Ezzra", "pull_pack3");

    EXPECT_EQ(move.Stop("the rule drink"), "Ezzra stops before pull_pack3: the rule drink");
    EXPECT_FALSE(move.InProgress());
    EXPECT_EQ(move.StopReason(), "the rule drink");
    EXPECT_EQ(move.Stopped(), 1u);
}

TEST(ScriptedMove, OnlyAMoveInProgressArrivesOrStops)
{
    ScriptedMove move;
    EXPECT_EQ(move.Arrive(), "");
    EXPECT_EQ(move.Stop("the rule drink"), "");

    move.Start("Ezzra", "p1");
    move.Stop("the rule drink");
    EXPECT_EQ(move.Arrive(), "");
    EXPECT_EQ(move.Stop("the rule loot a corpse"), "");
    EXPECT_EQ(move.StopReason(), "the rule drink");
    EXPECT_EQ(move.Stopped(), 1u);
}

// The fail of `wait arrive` names the stop of the move that it waits for.
TEST(ScriptedMove, ANewMoveForgetsTheStopOfTheLastOne)
{
    ScriptedMove move;
    move.Start("Ezzra", "p1");
    move.Stop("the rule drink");

    move.Start("Ezzra", "p2");
    EXPECT_EQ(move.StopReason(), "");
    move.Stop("the core's motion change to idle");
    EXPECT_EQ(move.Stopped(), 2u);
}

// A teleport or the end of the run ends a move with no line.
TEST(ScriptedMove, EndEndsAMoveWithoutAStop)
{
    ScriptedMove move;
    move.Start("Ezzra", "p1");
    move.End();

    EXPECT_FALSE(move.InProgress());
    EXPECT_EQ(move.StopReason(), "");
    EXPECT_EQ(move.Stopped(), 0u);
}
