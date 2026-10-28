#include "ScriptedMove.h"
#include "ScriptRunner.h"

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
    EXPECT_EQ(move.StopCount(), 0u);
}

TEST(ScriptedMove, AMoveThatStopsNamesItsReasonAndCounts)
{
    ScriptedMove move;
    move.Start("Ezzra", "pull_pack3");

    EXPECT_EQ(move.Stop("the rule drink"), "Ezzra stops before pull_pack3: the rule drink");
    EXPECT_FALSE(move.InProgress());
    EXPECT_EQ(move.StopReason(), "the rule drink");
    EXPECT_EQ(move.StopCount(), 1u);
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
    EXPECT_EQ(move.StopCount(), 1u);
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
    EXPECT_EQ(move.StopCount(), 2u);
}

// The Earthborer of run 1 in jvbastel/WOWAI#128: "follow the tank to the
// pull" takes the driver off its move to p51 in a fight. The move waits,
// still in progress so that the driver does not rest, and goes on when the
// group was out of combat for CombatEndQuietMs.
TEST(ScriptedMove, AMoveThatStopsInAFightGoesOnAfterIt)
{
    ScriptedMove move;
    move.Start("Ezzra", "p51");

    EXPECT_EQ(move.Stop("the rule follow the tank to the pull", true),
        "Ezzra stops before p51 for the fight: the rule follow the tank to the pull");
    EXPECT_TRUE(move.InProgress());
    EXPECT_TRUE(move.Waits());
    EXPECT_EQ(move.StopReason(), "the rule follow the tank to the pull");
    EXPECT_EQ(move.StopCount(), 1u);

    EXPECT_FALSE(move.FightEnded(true, 1000));
    EXPECT_FALSE(move.FightEnded(false, 9000));
    EXPECT_FALSE(move.FightEnded(false, 9000 + Bots::CombatEndQuietMs - 1));
    EXPECT_TRUE(move.FightEnded(false, 9000 + Bots::CombatEndQuietMs));

    EXPECT_EQ(move.GoOn(), "Ezzra goes on to p51 after the fight");
    EXPECT_TRUE(move.InProgress());
    EXPECT_FALSE(move.Waits());
    EXPECT_EQ(move.Arrive(), "Ezzra arrives at p51");
}

// A short gap between two enemies is no end of the fight.
TEST(ScriptedMove, AGapInTheFightDoesNotEndTheWait)
{
    ScriptedMove move;
    move.Start("Ezzra", "p51");
    move.Stop("the rule follow the tank to the pull", true);

    EXPECT_FALSE(move.FightEnded(false, 0));
    EXPECT_FALSE(move.FightEnded(true, 2000));
    EXPECT_FALSE(move.FightEnded(false, 3000));
    EXPECT_FALSE(move.FightEnded(false, 3000 + Bots::CombatEndQuietMs - 1));
    EXPECT_TRUE(move.FightEnded(false, 3000 + Bots::CombatEndQuietMs));
}

// A waiting move does not stop again in the same fight, and it goes on
// at most once; after it goes on, the next stop counts again.
TEST(ScriptedMove, AWaitingMoveStopsAndGoesOnOnce)
{
    ScriptedMove move;
    move.Start("Ezzra", "p51");
    move.Stop("the rule follow the tank to the pull", true);

    EXPECT_EQ(move.Stop("the rule frostbolt", true), "");
    EXPECT_EQ(move.StopCount(), 1u);
    EXPECT_NE(move.GoOn(), "");
    EXPECT_EQ(move.GoOn(), "");
    EXPECT_FALSE(move.FightEnded(false, 100000));

    EXPECT_EQ(move.Stop("the core's motion change to idle"), "Ezzra stops before p51: the core's motion change to idle");
    EXPECT_FALSE(move.InProgress());
    EXPECT_EQ(move.StopCount(), 2u);
}

// A teleport or the end of the run ends a move with no line.
TEST(ScriptedMove, EndEndsAMoveWithoutAStop)
{
    ScriptedMove move;
    move.Start("Ezzra", "p1");
    move.End();

    EXPECT_FALSE(move.InProgress());
    EXPECT_EQ(move.StopReason(), "");
    EXPECT_EQ(move.StopCount(), 0u);
}
