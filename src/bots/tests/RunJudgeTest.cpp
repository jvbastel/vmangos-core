#include "RunJudge.h"

#include <gtest/gtest.h>

using Bots::RunJudge;
using Bots::RunVerdict;
using Bots::RunVerdictKind;

namespace
{
    // The four bosses of Ragefire Chasm.
    std::vector<std::uint32_t> const RfcBosses = {11517, 11520, 11518, 11519};
    std::int64_t const Minute = 60000;
}

TEST(RunJudge, AFullRunPassesWhenTheFourthBossDies)
{
    RunJudge judge(RfcBosses, 0);
    judge.NoteBossKill(11517, 1 * Minute);
    judge.NoteBossKill(11520, 2 * Minute);
    judge.NoteBossKill(11518, 3 * Minute);
    EXPECT_EQ(judge.Judge(3 * Minute).kind, RunVerdictKind::Running);

    judge.NoteBossKill(11519, 4 * Minute);
    EXPECT_EQ(judge.Judge(4 * Minute).kind, RunVerdictKind::Passed);
}

TEST(RunJudge, AKillOfTheSameBossOrOfAnotherCreatureCountsOnce)
{
    RunJudge judge(RfcBosses, 0);
    for (int i = 0; i < 4; ++i)
        judge.NoteBossKill(11517, Minute);
    judge.NoteBossKill(11320, Minute);

    EXPECT_EQ(judge.Judge(Minute).kind, RunVerdictKind::Running);
}

TEST(RunJudge, AScenarioNeverPassesOnBossKills)
{
    // A scenario passes at the end of its commands (the ScriptRunner).
    RunJudge judge({}, 0);
    judge.NoteBossKill(11520, Minute);

    EXPECT_EQ(judge.Judge(Minute).kind, RunVerdictKind::Running);
}

TEST(RunJudge, OneWipeIsAllowedAndTheSecondFails)
{
    RunJudge judge(RfcBosses, 0);
    judge.NoteWipe();
    EXPECT_EQ(judge.Judge(Minute).kind, RunVerdictKind::Running);
    EXPECT_EQ(judge.Wipes(), 1u);

    judge.NoteWipe();
    RunVerdict const verdict = judge.Judge(2 * Minute);
    EXPECT_EQ(verdict.kind, RunVerdictKind::Failed);
    EXPECT_EQ(verdict.reason, "the second wipe");
}

TEST(RunJudge, ARunFailsAfterFortyFiveMinutes)
{
    RunJudge judge(RfcBosses, 1000);
    for (std::int64_t t = 1000; t < 46 * Minute; t += Minute)
        judge.NoteProgress(t);

    EXPECT_EQ(judge.Judge(1000 + 45 * Minute - 1).kind, RunVerdictKind::Running);
    RunVerdict const verdict = judge.Judge(1000 + 45 * Minute);
    EXPECT_EQ(verdict.kind, RunVerdictKind::Failed);
    EXPECT_EQ(verdict.reason, "the run did not end in 45 min");
}

TEST(RunJudge, ARunFailsAfterThreeMinutesWithNoProgress)
{
    RunJudge judge(RfcBosses, 0);
    judge.NoteProgress(10 * Minute);

    EXPECT_EQ(judge.Judge(13 * Minute - 1).kind, RunVerdictKind::Running);
    RunVerdict const verdict = judge.Judge(13 * Minute);
    EXPECT_EQ(verdict.kind, RunVerdictKind::Failed);
    EXPECT_EQ(verdict.reason, "no progress for 3 min: no kill, no damage dealt, and no movement along the route");
}

TEST(RunJudge, TheStartAndABossKillCountAsProgress)
{
    RunJudge judge(RfcBosses, 5 * Minute);
    EXPECT_EQ(judge.Judge(8 * Minute - 1).kind, RunVerdictKind::Running);

    judge.NoteBossKill(11517, 7 * Minute);
    EXPECT_EQ(judge.Judge(10 * Minute - 1).kind, RunVerdictKind::Running);
    EXPECT_EQ(judge.Judge(10 * Minute).kind, RunVerdictKind::Failed);
}

TEST(RunJudge, AFullRunFailsWhenItsRouteEndsWithABossAlive)
{
    // A wipe on the last pack ends its fight too: the route ends, but the
    // fourth boss lives.
    RunJudge judge(RfcBosses, 0);
    judge.NoteBossKill(11517, Minute);
    judge.NoteBossKill(11520, 2 * Minute);
    judge.NoteBossKill(11518, 3 * Minute);

    RunVerdict const verdict = judge.AtScriptEnd();
    EXPECT_EQ(verdict.kind, RunVerdictKind::Failed);
    EXPECT_EQ(verdict.reason, "the route ended with 3 of 4 bosses dead");
}

TEST(RunJudge, AScenarioPassesAtTheEndOfItsScript)
{
    EXPECT_EQ(RunJudge({}, 0).AtScriptEnd().kind, RunVerdictKind::Passed);
}

TEST(RunJudge, TheLastBossKillWinsOverALimitOfTheSameUpdate)
{
    RunJudge judge(RfcBosses, 0);
    judge.NoteWipe();
    for (std::uint32_t boss : RfcBosses)
        judge.NoteBossKill(boss, 2 * Minute);
    judge.NoteWipe();

    EXPECT_EQ(judge.Judge(2 * Minute).kind, RunVerdictKind::Passed);
}

// jvbastel/WOWAI#113: a threat loss never fails a run, on the healer or not.
// The judge counts them for the result.
TEST(RunJudge, NoNumberOfThreatLossesFailsARun)
{
    RunJudge scenario({}, 0);
    RunJudge full(RfcBosses, 0);
    for (int i = 0; i < 100; ++i)
    {
        scenario.NoteThreatLoss(i % 2 == 0);
        full.NoteThreatLoss(i % 2 == 0);
    }

    EXPECT_EQ(scenario.Judge(Minute).kind, RunVerdictKind::Running);
    EXPECT_EQ(scenario.AtScriptEnd().kind, RunVerdictKind::Passed);
    EXPECT_EQ(full.Judge(Minute).kind, RunVerdictKind::Running);
    EXPECT_EQ(scenario.ThreatLosses(), 100u);
    EXPECT_EQ(scenario.HealerThreatLosses(), 50u);
}
