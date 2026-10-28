#include "RunJudge.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

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

TEST(RunJudge, ARunFailsAfterSeventyMinutes)
{
    RunJudge judge(RfcBosses, 1000);
    for (std::int64_t t = 1000; t < 71 * Minute; t += Minute)
        judge.NoteProgress(t);

    EXPECT_EQ(judge.Judge(1000 + 70 * Minute - 1).kind, RunVerdictKind::Running);
    RunVerdict const verdict = judge.Judge(1000 + 70 * Minute);
    EXPECT_EQ(verdict.kind, RunVerdictKind::Failed);
    EXPECT_EQ(verdict.reason, "the run did not end in 70 min");
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


// jvbastel/WOWAI#167: a scenario's `expect healer_threat_losses <n>` line
// (RunScript.h) limits the threat losses on the healer in each pull.
namespace
{
    Bots::ScriptLimits HealerLimit(std::uint32_t most)
    {
        Bots::ScriptLimits limits;
        limits.healerThreatLosses = {true, most, 205};
        return limits;
    }
}

TEST(RunJudge, HealerThreatLossesUpToTheLimitOfAPullPass)
{
    RunJudge judge({}, 0, HealerLimit(1));
    judge.NotePull("pack13");
    judge.NoteHealerThreatLoss("Searing Blade Cultist on Hela");

    EXPECT_EQ(judge.Judge(Minute).kind, RunVerdictKind::Running);
    EXPECT_EQ(judge.AtScriptEnd().kind, RunVerdictKind::Passed);
}

TEST(RunJudge, OneHealerThreatLossOverTheLimitOfAPullFailsTheRunWithTheCount)
{
    RunJudge judge({}, 0, HealerLimit(1));
    judge.NotePull("pack13");
    judge.NoteHealerThreatLoss("Searing Blade Cultist on Hela");
    judge.NoteHealerThreatLoss("Searing Blade Enforcer on Hela");

    RunVerdict const verdict = judge.Judge(Minute);
    EXPECT_EQ(verdict.kind, RunVerdictKind::Failed);
    EXPECT_EQ(verdict.reason, "healer threat losses 2 in pull 1 (pack13), over the limit 1 of line 205: "
                              "Searing Blade Cultist on Hela; Searing Blade Enforcer on Hela");
    EXPECT_EQ(judge.AtScriptEnd().reason, verdict.reason);
}

TEST(RunJudge, AZeroLimitFailsAtTheFirstHealerThreatLoss)
{
    RunJudge judge({}, 0, HealerLimit(0));
    judge.NotePull("Molten Elemental");
    EXPECT_EQ(judge.Judge(Minute).kind, RunVerdictKind::Running);

    judge.NoteHealerThreatLoss("Molten Elemental on Hela");

    EXPECT_EQ(judge.Judge(Minute).reason,
        "healer threat losses 1 in pull 1 (Molten Elemental), over the limit 0 of line 205: Molten Elemental on Hela");
}

TEST(RunJudge, EachPullHasItsOwnCountOfHealerThreatLosses)
{
    RunJudge judge({}, 0, HealerLimit(1));
    judge.NotePull("pack1");
    judge.NoteHealerThreatLoss("Ragefire Trogg on Hela");
    judge.NotePull("pack2");
    judge.NoteHealerThreatLoss("Ragefire Shaman on Hela");
    EXPECT_EQ(judge.Judge(Minute).kind, RunVerdictKind::Running);

    judge.NoteHealerThreatLoss("Ragefire Trogg on Hela");

    EXPECT_EQ(judge.Judge(Minute).reason, "healer threat losses 2 in pull 2 (pack2), over the limit 1 of line 205: "
                                          "Ragefire Shaman on Hela; Ragefire Trogg on Hela");
}

TEST(RunJudge, HealerThreatLossesBeforeTheFirstPullCountToo)
{
    RunJudge judge({}, 0, HealerLimit(0));
    judge.NoteHealerThreatLoss("Ragefire Trogg on Hela");

    EXPECT_EQ(judge.Judge(Minute).reason,
        "healer threat losses 1 before the first pull, over the limit 0 of line 205: Ragefire Trogg on Hela");
}

TEST(RunJudge, WithNoLimitHealerThreatLossesNeverFailTheRun)
{
    RunJudge judge({}, 0);
    judge.NotePull("pack13");
    for (int i = 0; i < 9; ++i)
        judge.NoteHealerThreatLoss("Ragefire Trogg on Hela");

    EXPECT_EQ(judge.Judge(Minute).kind, RunVerdictKind::Running);
    EXPECT_EQ(judge.AtScriptEnd().kind, RunVerdictKind::Passed);
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

// The three ways of a caster pack are alternatives (review of
// jvbastel/WOWAI#123, A1): a fight spot on the cleared way, a line-of-sight
// pull, or the area round the caster's spawn. Any one that keeps the pack
// out makes it avoidable.
TEST(ExtraPackJudge, ACasterPackWithAnUnsafeSpawnAreaAndASpotThatKeepsThePackOutMakesItAvoidable)
{
    // 12.8 yd from the caster's spawn: in reach there, far from the tank.
    PullAt pull;
    pull.fake.SetMana(Target, 400, 400);
    pull.AddEnemy(Other, {35, 8, 0}, 10.0f);

    ExtraPackMark const mark = pull.Mark({Other});

    EXPECT_TRUE(mark.avoidable);
    EXPECT_EQ(mark.reason, "a fight spot at 0.0 0.0 0.0 on the cleared way keeps it out");
}

TEST(ExtraPackJudge, ACasterPackWhoseSpawnAreaKeepsThePackOutMakesItAvoidable)
{
    // In reach of each spot of the cleared way, 37 yd from the spawn.
    PullAt pull;
    pull.fake.SetMana(Target, 400, 400);
    pull.AddEnemy(Other, {-12, 3, 0}, 25.0f);

    ExtraPackMark const mark = pull.Mark({Other});

    EXPECT_TRUE(mark.avoidable);
    EXPECT_EQ(mark.reason, "the area round the caster's spawn keeps it out");
    // Not so for a pack with no caster.
    PullAt plain;
    plain.AddEnemy(Other, {-12, 3, 0}, 25.0f);
    EXPECT_FALSE(plain.Mark({Other}).avoidable);
}

// One meaning of a pack (review of jvbastel/WOWAI#121 and #123, T4): the
// search splits the enemies into packs by PackLinkYards (the packs of the
// tank's warn), the run counts the packs of the route. A pull that brings a
// pack of the search brings each route pack of its enemies.
TEST(ExtraPackJudge, APullThatBringsAPackOfTheSearchBringsEachRoutePackInIt)
{
    // Two route packs 6 yd apart: one pack of the search. Only the first is
    // in reach of the spots near the tank (x > -7.7).
    Bots::Guid const A = 0xF130000302000044;
    Bots::Guid const B = 0xF130000302000055;
    PullAt pull;
    pull.AddEnemy(A, {5, 8, 0}, 10.0f);
    pull.AddEnemy(B, {5, 14, 0}, 1.0f);

    Bots::PullChoices const choices = pull.Choices({{"packA", {A}}, {"packB", {B}}});

    auto const at = std::find_if(choices.choices.begin(), choices.choices.end(), [](Bots::PullChoice const& choice) {
        return choice.way == "a fight spot at -4.0 0.0 0.0 on the cleared way";
    });
    ASSERT_NE(at, choices.choices.end());
    EXPECT_EQ(at->brings, (std::set<std::string>{"packA", "packB"}));
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

    // The Ragefire Trogg of pack10 nearest to pack9 (creature 48660), 12.3 yd
    // from the shaman's spawn: out of its assistance reach.
    Bots::Guid const Trogg10 = 0xF130002C0A00BCB4; // pack10
    Position const Trogg10At = {-262.22f, -29.89f, -60.61f};

    // `shamanAt`: where the shaman of pack9 stands at the pull. It wanders up
    // to 5 yd round its spawn (wander_distance 5).
    ExtraPackMark MarkPack10AtPullPack9(float rockX, Position const& shamanAt = Pack9)
    {
        FakeBot fake{Tank};
        fake.SetPosition(Tank, PullPack9);
        fake.SetPosition(Shaman, shamanAt);
        fake.AddEnemy(Shaman, shamanAt, 15.0f);
        fake.SetPosition(Shaman10, Pack10);
        fake.AddEnemy(Shaman10, Pack10, 15.0f);
        fake.SetPosition(Trogg10, Trogg10At);
        fake.AddEnemy(Trogg10, Trogg10At, 15.0f);
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
        return Bots::MarkExtraPack(Bots::FindPullChoices(fake, trail, Shaman, {{"pack10", {Shaman10, Trogg10}}}), "pack10", 1);
    }
}

TEST(ExtraPackJudge, ACasterPackWithASpotOutOfItsSightMakesTheOtherPackAvoidable)
{
    ExtraPackMark const mark = MarkPack10AtPullPack9(-219.0f);

    EXPECT_TRUE(mark.avoidable);
    EXPECT_EQ(mark.reason.rfind("a line-of-sight pull at ", 0), 0u) << mark.reason;
}

TEST(ExtraPackJudge, AtPack9WithNoSpotOutOfSightTheFightSpotOnTheWayStillKeepsPack10Out)
{
    // The spawn area of the shaman pulls pack10, but the fight spot where
    // the tank stands does not: an alternative (A1), so pack10 is avoidable.
    ExtraPackMark const mark = MarkPack10AtPullPack9(0.0f);

    EXPECT_TRUE(mark.avoidable);
    EXPECT_EQ(mark.reason, "a fight spot at -225.9 -32.9 -55.4 on the cleared way keeps it out");
}

TEST(ExtraPackJudge, AtPack9AShamanThatWanderedNearPack10MakesPack10Unavoidable)
{
    // caster_pack run 190110 of t102-review-fix-2: the shaman stood 4.3 yd
    // from its spawn, toward pack10, at the pull. pack10 is then in its
    // assistance reach (8 yd from the trogg): each pull brings it.
    ExtraPackMark const mark = MarkPack10AtPullPack9(0.0f, {-256.5f, -35.5f, -60.5f});

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

// A pack that a `join` of the script sends in is no fault of the tank, also
// with no mark from the pull.
TEST(ExtraPackJudge, AJoinOfTheScriptMakesAnExtraPackUnavoidable)
{
    ExtraPackMark const avoidable = {true, "a fight spot at -8.0 0.0 0.0 on the cleared way keeps it out"};
    Bots::ExtraPackCause join;
    join.scripted = true;

    ExtraPackMark const marked = Bots::JudgeExtraPack(true, &avoidable, join);
    ExtraPackMark const unmarked = Bots::JudgeExtraPack(true, nullptr, join);

    EXPECT_FALSE(marked.avoidable);
    EXPECT_EQ(marked.reason, "the script brought it (join)");
    EXPECT_FALSE(unmarked.avoidable);
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

// jvbastel/WOWAI#171: no damage bot chases more than the scenario's limit
// (`expect damage_chase_yards 25`) from the tank. The game layer tells the
// judge, at each update, the chase of each bot and how far it is from the
// living tank.
namespace
{
    Bots::ScriptLimits ChaseLimit(std::uint32_t yards)
    {
        Bots::ScriptLimits limits;
        limits.damageChaseYards = {true, yards, 190};
        return limits;
    }

    Bots::BotRole const Ranged = Bots::BotRole::Ranged;
}

TEST(RunJudgeChase, ADamageBotChaseUpToTheLimitPasses)
{
    RunJudge judge({}, 0, ChaseLimit(25));
    judge.NoteChase("Morzul", Ranged, "Ragefire Shaman", 10.0f, 1000);
    judge.NoteChase("Morzul", Ranged, "Ragefire Shaman", 25.0f, 2000);

    EXPECT_EQ(judge.Judge(Minute).kind, RunVerdictKind::Running);
    EXPECT_EQ(judge.AtScriptEnd().kind, RunVerdictKind::Passed);
}

TEST(RunJudgeChase, ADamageBotChaseBeyondTheLimitFailsTheRun)
{
    RunJudge judge({}, 0, ChaseLimit(25));
    judge.NoteChase("Morzul", Ranged, "Ragefire Shaman", 20.0f, 39000);
    judge.NoteChase("Morzul", Ranged, "Ragefire Shaman", 31.5f, 39200);
    // The first chase beyond the limit is the reason; a later one is not.
    judge.NoteChase("Shanka", Bots::BotRole::Melee, "Ragefire Trogg", 40.0f, 40000);

    RunVerdict const verdict = judge.Judge(Minute);
    EXPECT_EQ(verdict.kind, RunVerdictKind::Failed);
    EXPECT_EQ(verdict.reason, "Morzul chases Ragefire Shaman 31.5 yd from the tank at 39.2 s, over the limit 25 "
                              "of line 190");
    EXPECT_EQ(judge.AtScriptEnd().reason, verdict.reason);
}

TEST(RunJudgeChase, AMeleeBotCountsToo)
{
    RunJudge judge({}, 0, ChaseLimit(25));
    judge.NoteChase("Shanka", Bots::BotRole::Melee, "Ragefire Trogg", 25.5f, 1000);

    EXPECT_EQ(judge.Judge(Minute).kind, RunVerdictKind::Failed);
}

// The check is for damage bots: the tank goes where it must, and the
// healer's walk is the business of its own rules.
TEST(RunJudgeChase, TheTankAndTheHealerDoNotCount)
{
    RunJudge judge({}, 0, ChaseLimit(25));
    judge.NoteChase("Grukk", Bots::BotRole::Tank, "Ragefire Trogg", 40.0f, 1000);
    judge.NoteChase("Hela", Bots::BotRole::Healer, "Ragefire Trogg", 40.0f, 1000);

    EXPECT_EQ(judge.Judge(Minute).kind, RunVerdictKind::Running);
    EXPECT_EQ(judge.AtScriptEnd().kind, RunVerdictKind::Passed);
}

TEST(RunJudgeChase, NoLimitNoCheck)
{
    RunJudge judge({}, 0);
    judge.NoteChase("Morzul", Ranged, "Ragefire Shaman", 60.0f, 1000);

    EXPECT_EQ(judge.Judge(Minute).kind, RunVerdictKind::Running);
}

// A full run fails on it too, as on the other limits.
TEST(RunJudgeChase, AFullRunFailsOnAChaseBeyondTheLimit)
{
    RunJudge judge(RfcBosses, 0, ChaseLimit(25));
    judge.NoteChase("Morzul", Ranged, "Ragefire Shaman", 26.0f, 1000);

    EXPECT_EQ(judge.Judge(Minute).kind, RunVerdictKind::Failed);
}
