#include "ScriptRunner.h"

#include <gtest/gtest.h>

using Bots::RunFacts;
using Bots::RunStep;
using Bots::RunStepKind;
using Bots::ScriptCommandKind;
using Bots::ScriptRunner;

namespace
{
    std::vector<Bots::ScriptCommand> Commands(std::string const& lines)
    {
        Bots::RunScriptResult const result = Bots::ParseRunScript(
            "script rfc ragefire_chasm\ndriver Ezzra\ngroup Grukk\noutside 1 0 0 0\nscenario s\n" + lines);
        EXPECT_TRUE(result.ok) << result.error;
        return result.ok ? result.script.scenarios[0].commands : std::vector<Bots::ScriptCommand>();
    }

    RunFacts Idle()
    {
        return RunFacts();
    }

    RunFacts InCombat()
    {
        RunFacts facts;
        facts.groupInCombat = true;
        return facts;
    }
}

TEST(ScriptRunner, HandsOutEachActionOnceAndPassesAtTheEnd)
{
    ScriptRunner runner(Commands("command follow\nrefill\n"));

    RunStep step = runner.Update(Idle(), 0);
    ASSERT_EQ(step.kind, RunStepKind::Do);
    EXPECT_EQ(step.command.kind, ScriptCommandKind::Command);
    EXPECT_EQ(runner.Update(Idle(), 10).kind, RunStepKind::Wait);
    runner.Done(true, "", 10);

    step = runner.Update(Idle(), 20);
    ASSERT_EQ(step.kind, RunStepKind::Do);
    EXPECT_EQ(step.command.kind, ScriptCommandKind::Refill);
    runner.Done(true, "", 20);

    EXPECT_EQ(runner.Update(Idle(), 30).kind, RunStepKind::Passed);
    EXPECT_EQ(runner.Update(Idle(), 40).kind, RunStepKind::Passed);
}

TEST(ScriptRunner, AFailedActionOrExpectFailsTheRunWithItsLine)
{
    ScriptRunner runner(Commands("refill\nexpect boss_dead 11520\nrefill\n"));
    runner.Update(Idle(), 0);
    runner.Done(true, "", 0);
    ASSERT_EQ(runner.Update(Idle(), 0).command.kind, ScriptCommandKind::ExpectBossDead);
    runner.Done(false, "the boss lives", 0);

    RunStep const step = runner.Update(Idle(), 5);
    EXPECT_EQ(step.kind, RunStepKind::Failed);
    EXPECT_EQ(step.reason, "line 7: expect boss_dead 11520: the boss lives");
}

TEST(ScriptRunner, WaitsTheSecondsOfAWait)
{
    ScriptRunner runner(Commands("wait 20\nrefill\n"));

    EXPECT_EQ(runner.Update(Idle(), 1000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(Idle(), 20999).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(Idle(), 21000).kind, RunStepKind::Do);
}

TEST(ScriptRunner, ATeleportWaitsUntilTheGroupArrived)
{
    ScriptRunner runner(Commands("teleport camp\nrefill\n"));
    RunFacts teleporting;
    teleporting.teleporting = true;

    ASSERT_EQ(runner.Update(Idle(), 0).kind, RunStepKind::Do);
    runner.Done(true, "", 0);
    EXPECT_EQ(runner.Update(teleporting, 1000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(teleporting, 2000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(Idle(), 3000).command.kind, ScriptCommandKind::Refill);
}

TEST(ScriptRunner, ATeleportThatNeverEndsFails)
{
    ScriptRunner runner(Commands("teleport camp\n"));
    RunFacts teleporting;
    teleporting.teleporting = true;
    runner.Update(Idle(), 0);
    runner.Done(true, "", 0);

    EXPECT_EQ(runner.Update(teleporting, Bots::TeleportLimitMs - 1).kind, RunStepKind::Wait);
    RunStep const step = runner.Update(teleporting, Bots::TeleportLimitMs);
    EXPECT_EQ(step.kind, RunStepKind::Failed);
    EXPECT_EQ(step.reason, "line 6: teleport camp: the group did not arrive in 60 s");
}

TEST(ScriptRunner, WaitArriveEndsWhenTheDriverIsThere)
{
    ScriptRunner runner(Commands("wait arrive\nrefill\n"));
    RunFacts there;
    there.driverArrived = true;

    EXPECT_EQ(runner.Update(Idle(), 0).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(Idle(), 5000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(there, 6000).kind, RunStepKind::Do);
}

TEST(ScriptRunner, WaitArriveFailsAfterItsLimit)
{
    ScriptRunner runner(Commands("wait arrive\n"));
    runner.Update(Idle(), 0);

    RunStep const step = runner.Update(Idle(), Bots::ArriveLimitMs);
    EXPECT_EQ(step.kind, RunStepKind::Failed);
    EXPECT_EQ(step.reason, "line 6: wait arrive: the test driver did not arrive in 60 s");
}

// The run never sends a stopped move again: the fail names why the move of
// the test driver stopped (jvbastel/WOWAI#99).
TEST(ScriptRunner, WaitArriveNamesTheStopOfTheMove)
{
    ScriptRunner runner(Commands("wait arrive\n"));
    RunFacts stopped;
    stopped.driverMoveStop = "the rule drink";
    runner.Update(stopped, 0);

    RunStep const step = runner.Update(stopped, Bots::ArriveLimitMs);
    EXPECT_EQ(step.kind, RunStepKind::Failed);
    EXPECT_EQ(step.reason, "line 6: wait arrive: the test driver did not arrive in 60 s, its move stopped: the rule drink");
}

// A move that stops in a fight goes on after it (jvbastel/WOWAI#128): the
// limit counts from the end of the last fight, so a long fight on the way
// does not fail the run.
TEST(ScriptRunner, WaitArriveCountsItsLimitFromTheEndOfTheFight)
{
    ScriptRunner runner(Commands("wait arrive\nrefill\n"));
    RunFacts there;
    there.driverArrived = true;
    runner.Update(Idle(), 0);

    EXPECT_EQ(runner.Update(InCombat(), 50000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(InCombat(), 90000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(Idle(), 90000 + Bots::ArriveLimitMs - 1).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(there, 90000 + Bots::ArriveLimitMs - 1).kind, RunStepKind::Do);
}

TEST(ScriptRunner, WaitArriveFailsAfterItsLimitFromTheEndOfTheFight)
{
    ScriptRunner runner(Commands("wait arrive\n"));
    runner.Update(Idle(), 0);
    runner.Update(InCombat(), 30000);

    EXPECT_EQ(runner.Update(Idle(), 30000 + Bots::ArriveLimitMs - 1).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(Idle(), 30000 + Bots::ArriveLimitMs).kind, RunStepKind::Failed);
}

TEST(ScriptRunner, WaitCombatEndWaitsForAFightAndItsEnd)
{
    ScriptRunner runner(Commands("wait combat_end\nrefill\n"));

    // The tank walks to the boss first: no fight yet.
    EXPECT_EQ(runner.Update(Idle(), 0).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(Idle(), 8000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(InCombat(), 9000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(InCombat(), 60000).kind, RunStepKind::Wait);
    // A short gap between two enemies is no end of the fight.
    EXPECT_EQ(runner.Update(Idle(), 61000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(InCombat(), 62000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(Idle(), 63000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(Idle(), 63000 + Bots::CombatEndQuietMs - 1).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(Idle(), 63000 + Bots::CombatEndQuietMs).kind, RunStepKind::Do);
}

TEST(ScriptRunner, WaitCombatEndFailsWhenNoFightStarts)
{
    ScriptRunner runner(Commands("wait combat_end\n"));
    runner.Update(Idle(), 0);

    RunStep const step = runner.Update(Idle(), Bots::FightStartLimitMs);
    EXPECT_EQ(step.kind, RunStepKind::Failed);
    EXPECT_EQ(step.reason, "line 6: wait combat_end: no fight started in 60 s");
}

TEST(ScriptRunner, WaitCombatEndFailsWhenTheFightLastsTooLong)
{
    ScriptRunner runner(Commands("wait combat_end 100\n"));
    runner.Update(Idle(), 0);
    runner.Update(InCombat(), 5000);

    EXPECT_EQ(runner.Update(InCombat(), 104999).kind, RunStepKind::Wait);
    RunStep const step = runner.Update(InCombat(), 105000);
    EXPECT_EQ(step.kind, RunStepKind::Failed);
    EXPECT_EQ(step.reason, "line 6: wait combat_end 100: the fight did not end in 100 s");
}

TEST(ScriptRunner, TellsTheLineThatRuns)
{
    ScriptRunner runner(Commands("refill\nwait 5\n"));
    EXPECT_EQ(runner.Current(), "line 6: refill");
    runner.Update(Idle(), 0);
    runner.Done(true, "", 0);
    runner.Update(Idle(), 0);
    EXPECT_EQ(runner.Current(), "line 7: wait 5");
    runner.Update(Idle(), 5000);
    EXPECT_EQ(runner.Current(), "the end");
}

TEST(ScriptRunner, AFightPullsThenWaitsForTheEndOfTheFight)
{
    ScriptRunner runner(Commands("fight pack1 100\nrefill\n"));

    RunStep const step = runner.Update(Idle(), 0);
    ASSERT_EQ(step.kind, RunStepKind::Do);
    EXPECT_EQ(step.command.kind, ScriptCommandKind::Fight);
    EXPECT_EQ(step.command.name, "pack1");
    runner.Done(true, "", 0);

    EXPECT_EQ(runner.Update(Idle(), 5000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(InCombat(), 6000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(Idle(), 30000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(Idle(), 30000 + Bots::CombatEndQuietMs).command.kind, ScriptCommandKind::Refill);
}

TEST(ScriptRunner, AFightFailsWhenItLastsTooLong)
{
    ScriptRunner runner(Commands("fight pack1 100\n"));
    runner.Update(Idle(), 0);
    runner.Done(true, "", 0);
    runner.Update(InCombat(), 1000);

    RunStep const step = runner.Update(InCombat(), 101000);
    EXPECT_EQ(step.kind, RunStepKind::Failed);
    EXPECT_EQ(step.reason, "line 6: fight pack1 100: the fight did not end in 100 s");
}

TEST(ScriptRunner, AFightWithNothingToFightGoesOnAtOnce)
{
    // The pack died in an earlier fight: no fight starts, and that is fine.
    ScriptRunner runner(Commands("fight pack1\nrefill\n"));
    runner.Update(Idle(), 0);
    runner.NothingToDo();

    EXPECT_EQ(runner.Update(Idle(), 0).command.kind, ScriptCommandKind::Refill);
}

TEST(ScriptRunner, WaitRestedWaitsUntilNoBotRests)
{
    ScriptRunner runner(Commands("wait rested\nrefill\n"));
    RunFacts resting;
    resting.groupResting = true;

    EXPECT_EQ(runner.Update(resting, 0).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(resting, 30000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(Idle(), 31000).command.kind, ScriptCommandKind::Refill);
}

TEST(ScriptRunner, WaitRestedWaitsUntilNoMemberIsDead)
{
    // The tank died in the last fight: no pull until it lives again.
    ScriptRunner runner(Commands("wait rested 60\nrefill\n"));
    RunFacts dead;
    dead.memberDead = true;

    EXPECT_EQ(runner.Update(dead, 0).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(dead, 20000).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(Idle(), 21000).command.kind, ScriptCommandKind::Refill);

    ScriptRunner stuck(Commands("wait rested 60\n"));
    stuck.Update(dead, 0);
    RunStep const step = stuck.Update(dead, 60000);
    EXPECT_EQ(step.kind, RunStepKind::Failed);
    EXPECT_EQ(step.reason, "line 6: wait rested 60: a member is still dead after 60 s");
}

TEST(ScriptRunner, WaitRestedFailsAfterItsSeconds)
{
    ScriptRunner runner(Commands("wait rested 60\n"));
    RunFacts resting;
    resting.groupResting = true;
    runner.Update(resting, 0);

    EXPECT_EQ(runner.Update(resting, 59999).kind, RunStepKind::Wait);
    RunStep const step = runner.Update(resting, 60000);
    EXPECT_EQ(step.kind, RunStepKind::Failed);
    EXPECT_EQ(step.reason, "line 6: wait rested 60: the group still rests after 60 s");
}

TEST(RunEnd, TheGroupWaitsForThePendingChatOfTheRun)
{
    // Spec section 10: the last boss_kill, wipe, or loot_roll gets its line
    // before the run group ends.
    EXPECT_TRUE(Bots::RunEndWaitsForChat(0, true));
    EXPECT_TRUE(Bots::RunEndWaitsForChat(19999, true));
    EXPECT_FALSE(Bots::RunEndWaitsForChat(0, false));
    EXPECT_FALSE(Bots::RunEndWaitsForChat(5000, false));
}

TEST(RunEnd, TheWaitForChatEndsAfterTwentySeconds)
{
    // A slow service answers in 5 s, a line comes about 7 s later, and a
    // line more than 10 s late drops: 20 s is past each of them.
    EXPECT_FALSE(Bots::RunEndWaitsForChat(20000, true));
    EXPECT_FALSE(Bots::RunEndWaitsForChat(60000, true));
}

namespace
{
    RunFacts PackAlive()
    {
        RunFacts facts;
        facts.fightPackAlive = true;
        return facts;
    }

    // Runs the fight that the runner just handed out: a pull, a fight, and
    // the quiet end. `after` are the facts at the end of the fight.
    RunStep FightToItsEnd(ScriptRunner& runner, std::int64_t& now, RunFacts const& after)
    {
        runner.Done(true, "", now);
        runner.Update(InCombat(), now += 1000);
        runner.Update(after, now += 1000);
        return runner.Update(after, now += Bots::CombatEndQuietMs);
    }
}

TEST(ScriptRunner, APackAliveAfterItsFightIsFoughtAgainAfterTheRest)
{
    // The tank died, and the pack reset: the group rests, then pulls again.
    ScriptRunner runner(Commands("fight pack1 100\nrefill\n"));
    std::int64_t now = 0;
    ASSERT_EQ(runner.Update(Idle(), now).command.kind, ScriptCommandKind::Fight);

    RunFacts restingAndAlive = PackAlive();
    restingAndAlive.groupResting = true;
    EXPECT_EQ(FightToItsEnd(runner, now, restingAndAlive).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(restingAndAlive, now += 20000).kind, RunStepKind::Wait);

    RunStep const again = runner.Update(PackAlive(), now += 1000);
    ASSERT_EQ(again.kind, RunStepKind::Do);
    EXPECT_EQ(again.command.kind, ScriptCommandKind::Fight);
    EXPECT_EQ(again.command.name, "pack1");

    // The second fight kills the pack: the run goes on.
    EXPECT_EQ(FightToItsEnd(runner, now, Idle()).command.kind, ScriptCommandKind::Refill);
}

TEST(ScriptRunner, APackThatLivesAfterThreeFightsFailsTheRunWithItsName)
{
    ScriptRunner runner(Commands("fight pack1 100\nrefill\n"));
    std::int64_t now = 0;
    runner.Update(Idle(), now);

    // Nobody rests: each fight again pulls at once.
    for (int fight = 1; fight < 3; ++fight)
    {
        RunStep const again = FightToItsEnd(runner, now, PackAlive());
        ASSERT_EQ(again.kind, RunStepKind::Do) << fight;
        ASSERT_EQ(again.command.kind, ScriptCommandKind::Fight) << fight;
    }
    RunStep const step = FightToItsEnd(runner, now, PackAlive());
    EXPECT_EQ(step.kind, RunStepKind::Failed);
    EXPECT_EQ(step.reason, "line 6: fight pack1 100: pack1 still lives after 3 fights");
}

TEST(ScriptRunner, AFightAgainWalksBackToThePointOfThePull)
{
    // After a wipe the group stands at the dungeon entrance: it follows the
    // driver back to the last point, rests, holds its place, and pulls.
    ScriptRunner runner(Commands("move p3\nwait arrive\nwait rested\ncommand stay\nfight pack8\ncommand follow\n"));
    std::int64_t now = 0;
    RunFacts arrived;
    arrived.driverArrived = true;
    runner.Update(arrived, now);
    runner.Done(true, "", now);
    runner.Update(arrived, now);
    runner.Done(true, "", now);
    ASSERT_EQ(runner.Update(arrived, now).command.kind, ScriptCommandKind::Fight);
    RunFacts aliveAway = PackAlive();
    aliveAway.driverArrived = false;
    RunStep step = FightToItsEnd(runner, now, aliveAway);

    std::vector<std::string> again;
    for (int i = 0; i < 4; ++i)
    {
        if (i > 0)
            step = runner.Update(arrived, now += 1000);
        ASSERT_EQ(step.kind, RunStepKind::Do) << i;
        again.push_back(step.command.source);
        if (step.command.kind != ScriptCommandKind::Fight)
            runner.Done(true, "", now);
    }
    EXPECT_EQ(again, (std::vector<std::string>{"command follow", "move p3", "command stay", "fight pack8"}));
}

TEST(ScriptRunner, AFightAgainWaitsForTheDeadBeforeTheWalkBack)
{
    // A wipe ends the fight before the release: the group walks back only
    // after it lives again at the entrance.
    ScriptRunner runner(Commands("move p3\nwait arrive\nfight pack8\n"));
    std::int64_t now = 0;
    RunFacts arrived;
    arrived.driverArrived = true;
    runner.Update(arrived, now);
    runner.Done(true, "", now);
    ASSERT_EQ(runner.Update(arrived, now).command.kind, ScriptCommandKind::Fight);

    RunFacts wiped = PackAlive();
    wiped.memberDead = true;
    EXPECT_EQ(FightToItsEnd(runner, now, wiped).kind, RunStepKind::Wait);
    EXPECT_EQ(runner.Update(wiped, now += 5000).kind, RunStepKind::Wait);

    RunStep const follow = runner.Update(Idle(), now += 1000);
    ASSERT_EQ(follow.kind, RunStepKind::Do);
    EXPECT_EQ(follow.command.source, "command follow");
}

namespace
{
    Bots::Route WalkRoute()
    {
        return Bots::ParseRoute("route rfc 389\n"
                                "point p1 0 0 0 2\n"
                                "point p2 30 0 0 2\n"
                                "point p3 60 0 0 2\n"
                                "pack pack3 70 0 0\n"
                                "mob 3 11319\n")
            .route;
    }

    // Hands out the steps from `step` on, says each worked, and returns the
    // sources up to the next `fight`. The driver arrives at once.
    std::vector<std::string> StepsToTheFight(ScriptRunner& runner, RunStep step, RunFacts facts, std::int64_t& now)
    {
        facts.driverArrived = true;
        std::vector<std::string> sources;
        for (int i = 0; i < 20 && step.kind == RunStepKind::Do; ++i)
        {
            sources.push_back(step.command.source);
            if (step.command.kind == ScriptCommandKind::Fight)
                break;
            runner.Done(true, "", now);
            step = runner.Update(facts, now += 1000);
        }
        return sources;
    }
}

// After a wipe the group lives at the entrance: the walk back goes over the
// route points, one `wait arrive` each, not one long walk that can pass
// packs and take longer than the arrive limit (WOWAI#94 review).
TEST(ScriptRunner, AFightAgainWalksBackOverTheRoutePoints)
{
    ScriptRunner runner(Commands("move p3\nwait arrive\ncommand stay\nfight pack3\ncommand follow\n"), WalkRoute());
    std::int64_t now = 0;
    RunFacts arrived;
    arrived.driverArrived = true;
    runner.Update(arrived, now);
    runner.Done(true, "", now);
    ASSERT_EQ(runner.Update(arrived, now).command.kind, ScriptCommandKind::Command);
    runner.Done(true, "", now);
    ASSERT_EQ(runner.Update(arrived, now).command.kind, ScriptCommandKind::Fight);
    RunFacts aliveAway = PackAlive();
    aliveAway.driverArrived = false;
    RunStep const step = FightToItsEnd(runner, now, aliveAway);

    RunFacts atTheEntrance;
    atTheEntrance.driverAt = {1, 1, 0};
    EXPECT_EQ(StepsToTheFight(runner, step, atTheEntrance, now),
        (std::vector<std::string>{"command follow", "move p1", "move p2", "move p3", "command stay", "fight pack3"}));
}

TEST(ScriptRunner, AFightAgainIsMarked)
{
    ScriptRunner runner(Commands("fight pack1 100\n"));
    std::int64_t now = 0;
    EXPECT_EQ(runner.Update(Idle(), now).command.again, 0u);

    RunStep const again = FightToItsEnd(runner, now, PackAlive());
    ASSERT_EQ(again.command.kind, ScriptCommandKind::Fight);
    EXPECT_EQ(again.command.again, 1u);
}

// The limit of the fights again holds for each fight of a pack: a pack that
// died and is fought again later (a respawn) has its own (WOWAI#94 review).
TEST(ScriptRunner, ALaterFightOfAPackHasItsOwnFightsAgain)
{
    ScriptRunner runner(Commands("fight pack1 100\nfight pack1 100\n"));
    std::int64_t now = 0;
    runner.Update(Idle(), now);
    for (int fight = 1; fight < 3; ++fight)
        ASSERT_EQ(FightToItsEnd(runner, now, PackAlive()).command.kind, ScriptCommandKind::Fight) << fight;
    // The third fight kills it: the next `fight pack1`.
    RunStep const next = FightToItsEnd(runner, now, Idle());
    ASSERT_EQ(next.command.kind, ScriptCommandKind::Fight);
    EXPECT_EQ(next.command.again, 0u);

    RunStep const again = FightToItsEnd(runner, now, PackAlive());
    EXPECT_EQ(again.kind, RunStepKind::Do);
    EXPECT_EQ(again.command.again, 1u);
}

// A pack with no route point before it has no walk back over the route: the
// group goes back to the last `move` of the script (merger review of the
// WOWAI#91-#96 fixes).
TEST(ScriptRunner, AFightAgainWithNoRoutePointBeforeThePackGoesToTheLastMove)
{
    Bots::Route const route = Bots::ParseRoute("route rfc 389\n"
                                               "pack pack3 70 0 0\n"
                                               "mob 3 11319\n"
                                               "point p3 60 0 0 2\n")
                                  .route;
    ScriptRunner runner(Commands("move p3\nwait arrive\ncommand stay\nfight pack3\ncommand follow\n"), route);
    std::int64_t now = 0;
    RunFacts arrived;
    arrived.driverArrived = true;
    runner.Update(arrived, now);
    runner.Done(true, "", now);
    ASSERT_EQ(runner.Update(arrived, now).command.kind, ScriptCommandKind::Command);
    runner.Done(true, "", now);
    ASSERT_EQ(runner.Update(arrived, now).command.kind, ScriptCommandKind::Fight);
    RunFacts aliveAway = PackAlive();
    aliveAway.driverArrived = false;
    RunStep const step = FightToItsEnd(runner, now, aliveAway);

    EXPECT_EQ(StepsToTheFight(runner, step, Idle(), now),
        (std::vector<std::string>{"command follow", "move p3", "command stay", "fight pack3"}));
}
