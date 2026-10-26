#include "RunResult.h"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

using Bots::RunResult;
using Json = nlohmann::json;

namespace
{
    RunResult AFailedFullRun()
    {
        RunResult r;
        r.number = 3;
        r.script = "ragefire_chasm";
        r.scenario = "full";
        r.fullRun = true;
        r.quests = Bots::RunQuests::Skip;
        r.startedAt = "2026-10-26T10:15:00Z";
        r.stamp = "20261026-101500";
        r.durationMs = 1234500;
        r.outcome = Bots::RunOutcome::Failed;
        r.reason = "the second wipe";
        r.driver = "Ezzra";
        r.group = {"Grukk", "Hela"};
        r.bossKills = {{11517, "Oggleflint", 300000}};
        r.wipes = {600000, 1200000};
        r.deaths = {{"Hela", 590000}, {"Grukk", 600000}, {"Hela", 1199000}};
        r.pulls = {{"pack1", 61500}};
        r.threatLosses = {{"Earthborer", "Hela", 62000, 67000}};
        r.lostMobs = {};
        r.gearDraws = {{"Grukk", "warrior_tank", "1h_shield", {{"main_hand", 2489, "Two-Handed Sword"}}}};
        r.choices = {{"Ezzra", "p1", 2, 3, 0.5f, -8.25f, 1000}};
        Bots::ObjectiveProgress kill = {5723, Bots::QuestObjective(), 3};
        kill.objective.kind = Bots::QuestObjectiveKind::Kill;
        kill.objective.entry = 11318;
        kill.objective.count = 8;
        r.questProgress = {kill};
        r.traceFile = "ragefire_chasm-full-20261026-101500.trace.txt";
        return r;
    }
}

TEST(RunResult, NamesItsFilesByScriptScenarioAndStartTime)
{
    EXPECT_EQ(Bots::RunFileStem(AFailedFullRun()), "ragefire_chasm-full-20261026-101500");
}

TEST(RunResult, WritesTheHeadOfTheRun)
{
    Json const j = Json::parse(Bots::RunResultJson(AFailedFullRun()));

    EXPECT_EQ(j["run"], 3);
    EXPECT_EQ(j["script"], "ragefire_chasm");
    EXPECT_EQ(j["scenario"], "full");
    EXPECT_EQ(j["full_run"], true);
    EXPECT_EQ(j["quests"], false);
    EXPECT_EQ(j["started_at"], "2026-10-26T10:15:00Z");
    EXPECT_DOUBLE_EQ(j["duration_s"].get<double>(), 1234.5);
    EXPECT_EQ(j["result"], "failed");
    EXPECT_EQ(j["reason"], "the second wipe");
    EXPECT_EQ(j["trace"], "ragefire_chasm-full-20261026-101500.trace.txt");
}

TEST(RunResult, WritesTheBossesTheWipesAndTheDeathsOfEachMember)
{
    Json const j = Json::parse(Bots::RunResultJson(AFailedFullRun()));

    ASSERT_EQ(j["bosses_killed"].size(), 1u);
    EXPECT_EQ(j["bosses_killed"][0]["entry"], 11517);
    EXPECT_EQ(j["bosses_killed"][0]["name"], "Oggleflint");
    EXPECT_DOUBLE_EQ(j["bosses_killed"][0]["time_s"].get<double>(), 300.0);
    EXPECT_EQ(j["wipes"], Json::parse("[600.0, 1200.0]"));
    // Each member, also one that never died, and the driver first.
    EXPECT_EQ(j["deaths"], Json::parse(R"({"Ezzra": [], "Grukk": [600.0], "Hela": [590.0, 1199.0]})"));
}

TEST(RunResult, WritesThePullsTheThreatLossesAndTheLostMobs)
{
    Json const j = Json::parse(Bots::RunResultJson(AFailedFullRun()));

    EXPECT_EQ(j["pulls"], Json::parse(R"([{"pack": "pack1", "time_s": 61.5}])"));
    EXPECT_EQ(j["threat_losses"],
        Json::parse(R"([{"enemy": "Earthborer", "member": "Hela", "time_s": 62.0, "seconds": 5.0}])"));
    EXPECT_EQ(j["lost_mobs"], Json::array());
}

TEST(RunResult, WritesTheGearDrawsAndTheRandomChoices)
{
    Json const j = Json::parse(Bots::RunResultJson(AFailedFullRun()));

    EXPECT_EQ(j["gear_draws"], Json::parse(R"([{"member": "Grukk", "template": "warrior_tank", "shape": "1h_shield",
        "items": [{"position": "main_hand", "entry": 2489, "name": "Two-Handed Sword"}]}])"));
    EXPECT_EQ(j["random_choices"], Json::parse(R"([{"who": "Ezzra", "point": "p1", "place": 2, "places": 3,
        "x": 0.5, "y": -8.25, "time_s": 1.0}])"));
}

TEST(RunResult, WritesTheProgressOfEachQuestObjective)
{
    Json const j = Json::parse(Bots::RunResultJson(AFailedFullRun()));

    EXPECT_EQ(j["quest_objectives"], Json::parse(R"([{"quest": 5723, "kind": "kill", "entry": 11318, "done": 3,
        "needed": 8, "complete": false}])"));
}

TEST(RunResult, APassedRunHasNoReasonAndNoTrace)
{
    RunResult r = AFailedFullRun();
    r.outcome = Bots::RunOutcome::Passed;
    r.reason.clear();
    r.traceFile.clear();
    Json const j = Json::parse(Bots::RunResultJson(r));

    EXPECT_EQ(j["result"], "passed");
    EXPECT_TRUE(j["reason"].is_null());
    EXPECT_TRUE(j["trace"].is_null());

    r.outcome = Bots::RunOutcome::Aborted;
    EXPECT_EQ(Json::parse(Bots::RunResultJson(r))["result"], "aborted");
}
