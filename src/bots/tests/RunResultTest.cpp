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
        r.pulls = {{"pack1", "Earthborer", 61500}, {"pack1", "Earthborer", 120000, true},
            {"", "Taragaman the Hungerer", 900000}};
        r.threatLosses = {{"Earthborer", "Hela", 62000, 67000}};
        r.lostMobs = {};
        r.gearDraws = {{"Grukk", "warrior_tank", "1h_shield", {{"main_hand", 2489, "Two-Handed Sword"}}}};
        r.choices = {{"Ezzra", "p1", 2, 3, 0.5f, -8.25f, 1000}};
        Bots::ObjectiveProgress kill = {5723, Bots::QuestObjective(), 3};
        kill.objective.kind = Bots::QuestObjectiveKind::Kill;
        kill.objective.entry = 11318;
        kill.objective.count = 8;
        r.questProgress = {kill};
        r.interrupts = {{"Shanka", "Searing Blade Cultist", 332, "Healing Wave", Bots::CastKind::Heal, 70000}};
        r.missedCasts = {{"Searing Blade Cultist", 403, "Lightning Bolt", Bots::CastKind::Harm, 75000}};
        r.doubleInterrupts = {{"Searing Blade Cultist", 332, "Healing Wave", "Shanka", "Grukk", 70500}};
        r.chases = {{"Shanka", "Earthborer", 80000, 82500, 31.25f}};
        r.fireNovaHits = {{"Hela", Bots::BotRole::Healer, 1, 85, 905000}};
        r.chaseLimitYards = 25.0f;
        r.chatEvents = {{"boss_kill", "", 300000, true, {{"Grukk", 302000}, {"Hela", 304500}}},
            {"player_message", "Hela", 20000, false, {}}};
        Bots::FightSummary fight;
        fight.number = 2;
        fight.pack = "pack1";
        fight.again = true;
        fight.startMs = 120000;
        fight.endMs = 135500;
        fight.members = {{"Grukk", true, 400, 1}, {"Hela", false, 0, 0}};
        r.fights = {fight};
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

    // A `command pull` of a scenario has no pack.
    // A fight again of a pack that lived after its fight is marked.
    EXPECT_EQ(j["pulls"], Json::parse(R"([{"pack": "pack1", "target": "Earthborer", "time_s": 61.5, "again": false,
        "extra_packs": []},
        {"pack": "pack1", "target": "Earthborer", "time_s": 120.0, "again": true, "extra_packs": []},
        {"pack": null, "target": "Taragaman the Hungerer", "time_s": 900.0, "again": false, "extra_packs": []}])"));
    EXPECT_EQ(j["threat_losses"],
        Json::parse(R"([{"enemy": "Earthborer", "member": "Hela", "time_s": 62.0, "seconds": 5.0}])"));
    EXPECT_EQ(j["lost_mobs"], Json::array());
}

// jvbastel/WOWAI#103: the packs that joined the fight of a pull, and
// whether the tank warned about them in party chat.
TEST(RunResult, WritesTheExtraPacksOfEachPull)
{
    RunResult r = AFailedFullRun();
    r.pulls[0].extraPacks = {{"pack2", 64000, true}, {"pack3", 70500, false}};

    Json const j = Json::parse(Bots::RunResultJson(r));

    EXPECT_EQ(j["pulls"][0]["extra_packs"], Json::parse(R"([{"pack": "pack2", "time_s": 64.0, "warned": true},
        {"pack": "pack3", "time_s": 70.5, "warned": false}])"));
    EXPECT_EQ(j["pulls"][1]["extra_packs"], Json::array());
    EXPECT_EQ(j["extra_packs"], 2);
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

TEST(RunResult, WritesTheMembersOfTheRun)
{
    Json const j = Json::parse(Bots::RunResultJson(AFailedFullRun()));

    EXPECT_EQ(j["driver"], "Ezzra");
    EXPECT_EQ(j["group"], Json::parse(R"(["Grukk", "Hela"])"));
}

TEST(RunResult, WritesTheInterruptsDoneMissedAndDouble)
{
    Json const j = Json::parse(Bots::RunResultJson(AFailedFullRun()));

    EXPECT_EQ(j["interrupts"], Json::parse(R"([{"member": "Shanka", "enemy": "Searing Blade Cultist", "spell": 332,
        "spell_name": "Healing Wave", "kind": "heal", "time_s": 70.0}])"));
    EXPECT_EQ(j["missed_interrupts"], Json::parse(R"([{"enemy": "Searing Blade Cultist", "spell": 403,
        "spell_name": "Lightning Bolt", "kind": "harm", "time_s": 75.0}])"));
    EXPECT_EQ(j["double_interrupts"], Json::parse(R"([{"enemy": "Searing Blade Cultist", "spell": 332,
        "spell_name": "Healing Wave", "first": "Shanka", "second": "Grukk", "time_s": 70.5}])"));
}

TEST(RunResult, WritesTheChasesBeyondTheLimitAndTheFireNovaHits)
{
    Json const j = Json::parse(Bots::RunResultJson(AFailedFullRun()));

    EXPECT_EQ(j["chases"], Json::parse(R"([{"member": "Shanka", "enemy": "Earthborer", "time_s": 80.0,
        "seconds": 2.5, "max_yards": 31.25}])"));
    EXPECT_EQ(j["chase_limit_yards"], 25.0);
    EXPECT_EQ(j["fire_nova_hits"], Json::parse(R"([{"member": "Hela", "role": "healer", "fight": 1, "damage": 85,
        "time_s": 905.0}])"));
}

TEST(RunResult, WritesTheChatEventsWithTheLinesThatTheBotsSaid)
{
    Json const j = Json::parse(Bots::RunResultJson(AFailedFullRun()));

    // `to`: the whispered bot of a player message.
    EXPECT_EQ(j["chat_events"], Json::parse(R"([
        {"event": "boss_kill", "to": null, "time_s": 300.0, "reached": true,
         "lines": [{"bot": "Grukk", "time_s": 302.0}, {"bot": "Hela", "time_s": 304.5}]},
        {"event": "player_message", "to": "Hela", "time_s": 20.0, "reached": false, "lines": []}])"));
}

TEST(RunResult, WritesWhoTookPartInEachFight)
{
    Json const j = Json::parse(Bots::RunResultJson(AFailedFullRun()));

    EXPECT_EQ(j["fights"], Json::parse(R"([{"pull": 2, "pack": "pack1", "again": true, "time_s": 120.0,
        "seconds": 15.5, "members": {
            "Grukk": {"joined": true, "first_action_s": 0.4, "casts": 1},
            "Hela": {"joined": false, "first_action_s": null, "casts": 0}}}])"));
}

// The fight of a Fire Nova hit: 1 for the first pull of the fight, one more
// for each pull again of it, with or without a wipe (WOWAI#94/#95 review).
TEST(RunResult, TheFightNumberCountsThePullsAgain)
{
    EXPECT_EQ(Bots::FightNumber({}), 1u);
    EXPECT_EQ(Bots::FightNumber({{"pack1", "Earthborer", 1000}}), 1u);
    EXPECT_EQ(Bots::FightNumber({{"pack1", "Earthborer", 1000}, {"pack1", "Earthborer", 2000, true},
                  {"pack1", "Earthborer", 3000, true}}),
        3u);
    EXPECT_EQ(Bots::FightNumber({{"pack1", "Earthborer", 1000}, {"pack1", "Earthborer", 2000, true},
                  {"pack2", "Taragaman the Hungerer", 3000}}),
        1u);
}
