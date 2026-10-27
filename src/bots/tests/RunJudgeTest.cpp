#include "RunJudge.h"
#include "FakeBot.h"

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


// jvbastel/WOWAI#123: the judge marks each extra pack avoidable or
// unavoidable. It runs the fight spot rules of #105 and #121 (FightSpot.h)
// again for the pack, on the real packs at the pull, from the tank's
// cleared way.
namespace
{
    using Bots::ExtraPackMark;
    using Bots::Position;
    using Bots::Tests::FakeBot;

    Bots::Guid const Tank = 0x11;
    Bots::Guid const Target = 0xF130000302000022;
    Bots::Guid const Other = 0xF130000302000033;

    // A tank at (0, 0, 0) that walked along the x axis from x = -30, and a
    // pull target 25 yd ahead of it.
    struct PullAt
    {
        FakeBot fake{Tank};
        std::vector<Position> trail;

        PullAt()
        {
            fake.SetPosition(Tank, {0, 0, 0});
            for (float x = -30.0f; x <= 0.0f; x += 2.0f)
                trail.push_back({x, 0.0f, 0.0f});
            AddEnemy(Target, {25, 0, 0}, 10.0f);
        }

        void AddEnemy(Bots::Guid enemy, Position at, float aggroYards)
        {
            fake.SetPosition(enemy, at);
            fake.AddEnemy(enemy, at, aggroYards);
        }

        // The pull choices with the route packs `packs` round the pull.
        Bots::PullChoices Choices(std::vector<Bots::RoutePackAtPull> const& packs) const
        {
            return Bots::FindPullChoices(fake, trail, Target, packs);
        }

        // The mark of the one route pack `pack`, when the pull brought
        // `got` extra packs.
        ExtraPackMark Mark(std::vector<Bots::Guid> const& pack, std::size_t got = 1) const
        {
            return Bots::MarkExtraPack(Choices({{"other", pack}}), "other", got);
        }
    };
}

TEST(ExtraPackJudge, AnExtraPackIsAvoidableWhenASpotOnTheClearedWayKeepsItOut)
{
    // The tank stands in the aggro distance and margin of the other pack;
    // x = -8 back on its way is out of it (FightSpotTest).
    PullAt pull;
    pull.AddEnemy(Other, {5, 8, 0}, 10.0f);

    ExtraPackMark const mark = pull.Mark({Other});

    EXPECT_TRUE(mark.avoidable);
    EXPECT_EQ(mark.reason, "a fight spot at -8.0 0.0 0.0 on the cleared way keeps it out");
}

TEST(ExtraPackJudge, AnExtraPackThatAnswersTheCallForHelpOfTheTargetIsUnavoidable)
{
    // 9 yd from the target: within the assistance radius, wherever the
    // tank fights.
    PullAt pull;
    pull.AddEnemy(Other, {25, 9, 0}, 10.0f);

    ExtraPackMark const mark = pull.Mark({Other});

    EXPECT_FALSE(mark.avoidable);
    EXPECT_EQ(mark.reason, "no fight spot, line-of-sight pull or spawn area keeps it out with at most 1 extra pack");
}

// The judge runs the search with all the packs round the pull, as the tank
// did (review of jvbastel/WOWAI#123, C3): a pack is avoidable only when a
// pull kept it out with no more extra packs in all than the tank got.
TEST(ExtraPackJudge, APackIsAvoidableOnlyByAPullWithNoMoreExtraPacksThanTheTankGot)
{
    // Pack A stands in reach of the spots near the tank (x > -7.7). Each
    // spot farther back on the way pulls packs B and C.
    Bots::Guid const A = 0xF130000302000044;
    Bots::Guid const B = 0xF130000302000055;
    Bots::Guid const C = 0xF130000302000066;
    PullAt pull;
    pull.AddEnemy(A, {5, 8, 0}, 10.0f);
    pull.AddEnemy(B, {-14, 10, 0}, 15.0f);
    pull.AddEnemy(C, {-26, 10, 0}, 20.0f);

    Bots::PullChoices const choices = pull.Choices({{"packA", {A}}, {"packB", {B}}, {"packC", {C}}});

    ExtraPackMark const alone = Bots::MarkExtraPack(choices, "packA", 1);
    EXPECT_FALSE(alone.avoidable);
    EXPECT_EQ(alone.reason, "no fight spot, line-of-sight pull or spawn area keeps it out with at most 1 extra pack");
    ExtraPackMark const withMore = Bots::MarkExtraPack(choices, "packA", 3);
    EXPECT_TRUE(withMore.avoidable);
    EXPECT_EQ(withMore.reason, "a fight spot at -8.0 0.0 0.0 on the cleared way keeps it out, with 2 other extra packs");
}

TEST(ExtraPackJudge, APackOutOfTheSearchAtThePullIsAvoidable)
{
    // Nothing of the rules of #121 saw it at the pull: the fight went to it.
    PullAt pull;

    ExtraPackMark const mark = pull.Mark({Other});

    EXPECT_TRUE(mark.avoidable);
    EXPECT_EQ(mark.reason, "it was out of the fight spot search at the pull: the fight went to it");
}

namespace
{
    // pack9 to pack11 of Ragefire Chasm and the way to pull_pack9, as in
    // CasterPackTest.cpp: pack9 is one caster, 16.8 yd from pack10.
    Bots::Guid const Shaman = 0xF130002C0B00BCBE;   // pack9
    Bots::Guid const Shaman10 = 0xF130002C0B00BCB1; // pack10
    Position const P17 = {-205.46f, -31.92f, -49.76f};
    Position const PullPack9 = {-225.85f, -32.92f, -55.42f};
    Position const Pack9 = {-253.89f, -38.88f, -60.46f};
    Position const Pack10 = {-266.73f, -28.03f, -60.61f};

    ExtraPackMark MarkPack10AtPullPack9(float rockX)
    {
        FakeBot fake{Tank};
        fake.SetPosition(Tank, PullPack9);
        fake.SetPosition(Shaman, Pack9);
        fake.AddEnemy(Shaman, Pack9, 15.0f);
        fake.SetPosition(Shaman10, Pack10);
        fake.AddEnemy(Shaman10, Pack10, 15.0f);
        fake.SetMana(Shaman, 400, 400);
        if (rockX != 0.0f)
            fake.AddSightWall({rockX, -40.0f, 0.0f}, {rockX, -31.0f, 0.0f});
        std::vector<Position> trail;
        for (int i = 0; i <= 10; ++i)
        {
            float const t = float(i) / 10.0f;
            trail.push_back({P17.x + (PullPack9.x - P17.x) * t, P17.y + (PullPack9.y - P17.y) * t,
                P17.z + (PullPack9.z - P17.z) * t});
        }
        return Bots::MarkExtraPack(Bots::FindPullChoices(fake, trail, Shaman, {{"pack10", {Shaman10}}}), "pack10", 1);
    }
}

TEST(ExtraPackJudge, ACasterPackWithASpotOutOfItsSightMakesTheOtherPackAvoidable)
{
    ExtraPackMark const mark = MarkPack10AtPullPack9(-219.0f);

    EXPECT_TRUE(mark.avoidable);
    EXPECT_EQ(mark.reason.rfind("a line-of-sight pull at ", 0), 0u) << mark.reason;
}

TEST(ExtraPackJudge, ACasterPackWithNoSpotOutOfSightAndAnUnsafeSpawnAreaMakesItUnavoidable)
{
    ExtraPackMark const mark = MarkPack10AtPullPack9(0.0f);

    EXPECT_FALSE(mark.avoidable);
    EXPECT_EQ(mark.reason, "no fight spot, line-of-sight pull or spawn area keeps it out with at most 1 extra pack");
}

TEST(ExtraPackJudge, AFleeOrAPatrolMakesAnExtraPackUnavoidable)
{
    ExtraPackMark const avoidable = {true, "a fight spot at -8.0 0.0 0.0 on the cleared way keeps it out"};
    Bots::ExtraPackCause flee;
    flee.fleeing = "Ragefire Shaman 48662";
    Bots::ExtraPackCause patrol;
    patrol.patrol = true;

    ExtraPackMark const fled = Bots::JudgeExtraPack(true, &avoidable, flee);
    ExtraPackMark const walked = Bots::JudgeExtraPack(true, &avoidable, patrol);
    ExtraPackMark const plain = Bots::JudgeExtraPack(true, &avoidable, {});

    EXPECT_FALSE(fled.avoidable);
    EXPECT_EQ(fled.reason, "a fleeing enemy brought it: Ragefire Shaman 48662");
    EXPECT_FALSE(walked.avoidable);
    EXPECT_EQ(walked.reason, "a patrol brought it");
    EXPECT_TRUE(plain.avoidable);
    EXPECT_EQ(plain.reason, avoidable.reason);
}

TEST(ExtraPackJudge, APackThatJoinsWithNoMarkFromThePullIsAvoidable)
{
    ExtraPackMark const mark = Bots::JudgeExtraPack(true, nullptr, {});

    EXPECT_TRUE(mark.avoidable);
    EXPECT_EQ(mark.reason, "it was out of the fight spot search at the pull: the fight went to it");
}

// A pull the run could not judge (no living tank, no target, another map)
// gives no false "out of the search" reason, and does not fail a warned pack.
TEST(ExtraPackJudge, APackOfAPullWithNoJudgeIsNotJudgedAndNotAvoidable)
{
    ExtraPackMark const mark = Bots::JudgeExtraPack(false, nullptr, {});
    Bots::ExtraPackCause patrol;
    patrol.patrol = true;
    ExtraPackMark const walked = Bots::JudgeExtraPack(false, nullptr, patrol);

    EXPECT_FALSE(mark.avoidable);
    EXPECT_EQ(mark.reason, "not judged: the run saw no tank and target at the pull");
    EXPECT_EQ(walked.reason, "a patrol brought it");
}

// Acceptance criteria 2 and 3: an unwarned or an avoidable extra pack fails
// the pull; a warned unavoidable one passes it.
namespace
{
    Bots::RunPull PullWith(std::vector<Bots::RunExtraPack> extra)
    {
        Bots::RunPull pull = {"pack48", "Ragefire Trogg", 1000, false, std::move(extra)};
        return pull;
    }

    Bots::RunExtraPack Extra(bool warned, bool avoidable)
    {
        Bots::RunExtraPack extra = {"pack49", 2000, warned};
        extra.avoidable = avoidable;
        extra.reason = avoidable ? "a fight spot at -8.0 0.0 0.0 on the cleared way keeps it out"
                                 : "no fight spot on the cleared way keeps it out";
        return extra;
    }
}

TEST(ExtraPackJudge, AWarnedUnavoidableExtraPackPassesThePull)
{
    EXPECT_EQ(Bots::PullFailure(PullWith({Extra(true, false)})), "");
    EXPECT_EQ(Bots::PullFailure(PullWith({})), "");
}

TEST(ExtraPackJudge, AnUnwarnedExtraPackFailsThePullEvenWhenUnavoidable)
{
    EXPECT_EQ(Bots::PullFailure(PullWith({Extra(false, false)})), "extra pack pack49 joined with no warn");
}

TEST(ExtraPackJudge, AnAvoidableExtraPackFailsThePullEvenWhenWarned)
{
    EXPECT_EQ(Bots::PullFailure(PullWith({Extra(true, true)})),
        "extra pack pack49 was avoidable: a fight spot at -8.0 0.0 0.0 on the cleared way keeps it out");
}

// User decision (2026-10-27): a failed pull is counted and shown, but it
// does not fail the run.
TEST(ExtraPackJudge, CountsTheFailedPullsOfARun)
{
    std::vector<Bots::RunPull> const pulls = {PullWith({Extra(true, false)}), PullWith({Extra(false, false)}),
        PullWith({Extra(true, true)}), PullWith({})};

    EXPECT_EQ(Bots::FailedPulls(pulls), 2u);
    EXPECT_EQ(Bots::FailedPulls({PullWith({Extra(true, false)})}), 0u);
}
