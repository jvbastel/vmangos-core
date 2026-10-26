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
