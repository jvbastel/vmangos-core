#include "RunScript.h"
#include "Route.h"

#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <sstream>

using Bots::ParseRunScript;
using Bots::RunScript;
using Bots::RunScriptResult;
using Bots::ScriptCommand;
using Bots::ScriptCommandKind;

namespace
{
    char const* const Script =
        "# The scripted runs of Ragefire Chasm\n"
        "script ragefire_chasm ragefire_chasm\n"
        "driver Ezzra\n"
        "group Grukk Hela Shanka Morzul\n"
        "outside 1 1811.5 -4410.25 -18.5\n"
        "scenario taragaman\n"
        "teleport taragaman_camp\n"
        "clear 45\n"
        "command follow\n"
        "wait 20\n"
        "refill\n"
        "target 11520 # Taragaman\n"
        "command pull\n"
        "wait combat_end 240\n"
        "expect boss_dead 11520\n"
        "scenario shamans\n"
        "teleport shaman_pull\n"
        "clear 40 shamans1\n"
        "move shaman_pull\n"
        "wait arrive\n"
        "whisper Grukk pull\n"
        "target shamans1\n"
        "wait combat_end\n"
        "expect pack_dead shamans1\n"
        "expect alive\n";

    char const* const RouteText =
        "route ragefire_chasm 389\n"
        "boss 11520\n"
        "point taragaman_camp -274.1 150.7 -21.6 2\n"
        "point shaman_pull -120 -20 -40 2\n"
        "pack shamans1 -142.4 -12.9 -54.3\n"
        "mob 43580 11319\n";

    std::string ErrorOf(std::string const& text)
    {
        RunScriptResult const result = ParseRunScript(text);
        EXPECT_FALSE(result.ok);
        return result.error;
    }

    std::string const Head =
        "script rfc ragefire_chasm\n"
        "driver Ezzra\n"
        "group Grukk Hela Shanka Morzul\n"
        "outside 1 0 0 0\n"
        "scenario s\n";
}

TEST(RunScript, ReadsTheHeadOfTheScript)
{
    RunScriptResult const result = ParseRunScript(Script);

    ASSERT_TRUE(result.ok) << result.error;
    RunScript const& script = result.script;
    EXPECT_EQ(script.name, "ragefire_chasm");
    EXPECT_EQ(script.route, "ragefire_chasm");
    EXPECT_EQ(script.driver, "Ezzra");
    EXPECT_EQ(script.group, (std::vector<std::string>{"Grukk", "Hela", "Shanka", "Morzul"}));
    EXPECT_EQ(script.outsideMap, 1u);
    EXPECT_FLOAT_EQ(script.outside.x, 1811.5f);
    EXPECT_FLOAT_EQ(script.outside.y, -4410.25f);
    EXPECT_FLOAT_EQ(script.outside.z, -18.5f);
}

// jvbastel/WOWAI#118: a script can set the level of the run (the Deadmines
// pack is about level 18). Without the line, the run is at the version 1
// level, 16.
TEST(RunScript, ReadsTheLevelOfTheRunOrTakesLevel16)
{
    RunScriptResult const plain = ParseRunScript(Script);
    ASSERT_TRUE(plain.ok) << plain.error;
    EXPECT_EQ(plain.script.level, 16u);

    RunScriptResult const level18 = ParseRunScript("level 18\n" + Head + "refill\n");
    ASSERT_TRUE(level18.ok) << level18.error;
    EXPECT_EQ(level18.script.level, 18u);

    EXPECT_EQ(ErrorOf("level x\n" + Head + "refill\n"), "line 1: use: level <1-60>");
    EXPECT_EQ(ErrorOf("level 0\n" + Head + "refill\n"), "line 1: use: level <1-60>");
    EXPECT_EQ(ErrorOf("level 61\n" + Head + "refill\n"), "line 1: use: level <1-60>");
}

TEST(RunScript, ReadsEachScenarioWithItsCommandsInOrder)
{
    RunScriptResult const result = ParseRunScript(Script);
    ASSERT_TRUE(result.ok) << result.error;

    ASSERT_EQ(result.script.scenarios.size(), 2u);
    Bots::ScriptScenario const* boss = Bots::FindScenario(result.script, "taragaman");
    ASSERT_NE(boss, nullptr);
    std::vector<ScriptCommand> const& c = boss->commands;
    ASSERT_EQ(c.size(), 9u);
    EXPECT_EQ(c[0].kind, ScriptCommandKind::Teleport);
    EXPECT_EQ(c[0].name, "taragaman_camp");
    EXPECT_EQ(c[0].line, 7u);
    EXPECT_EQ(c[1].kind, ScriptCommandKind::Clear);
    EXPECT_FLOAT_EQ(c[1].yards, 45.0f);
    EXPECT_EQ(c[1].name, "");
    EXPECT_EQ(c[2].kind, ScriptCommandKind::Command);
    EXPECT_EQ(c[2].text, "follow");
    EXPECT_EQ(c[3].kind, ScriptCommandKind::WaitSeconds);
    EXPECT_EQ(c[3].seconds, 20u);
    EXPECT_EQ(c[4].kind, ScriptCommandKind::Refill);
    EXPECT_EQ(c[5].kind, ScriptCommandKind::Target);
    EXPECT_EQ(c[5].entry, 11520u);
    EXPECT_EQ(c[5].name, "");
    EXPECT_EQ(c[6].kind, ScriptCommandKind::Command);
    EXPECT_EQ(c[6].text, "pull");
    EXPECT_EQ(c[7].kind, ScriptCommandKind::WaitCombatEnd);
    EXPECT_EQ(c[7].seconds, 240u);
    EXPECT_EQ(c[8].kind, ScriptCommandKind::ExpectBossDead);
    EXPECT_EQ(c[8].entry, 11520u);
    EXPECT_EQ(c[8].source, "expect boss_dead 11520");
}

TEST(RunScript, ReadsTheOtherCommands)
{
    RunScriptResult const result = ParseRunScript(Script);
    ASSERT_TRUE(result.ok) << result.error;

    std::vector<ScriptCommand> const& c = Bots::FindScenario(result.script, "shamans")->commands;
    ASSERT_EQ(c.size(), 9u);
    EXPECT_EQ(c[1].kind, ScriptCommandKind::Clear);
    EXPECT_EQ(c[1].packs, (std::vector<std::string>{"shamans1"}));
    EXPECT_EQ(c[2].kind, ScriptCommandKind::Move);
    EXPECT_EQ(c[2].name, "shaman_pull");
    EXPECT_EQ(c[3].kind, ScriptCommandKind::WaitArrive);
    EXPECT_EQ(c[4].kind, ScriptCommandKind::Whisper);
    EXPECT_EQ(c[4].name, "Grukk");
    EXPECT_EQ(c[4].text, "pull");
    EXPECT_EQ(c[5].kind, ScriptCommandKind::Target);
    EXPECT_EQ(c[5].name, "shamans1");
    EXPECT_EQ(c[6].kind, ScriptCommandKind::WaitCombatEnd);
    EXPECT_EQ(c[6].seconds, Bots::DefaultCombatEndSeconds);
    EXPECT_EQ(c[7].kind, ScriptCommandKind::ExpectPackDead);
    EXPECT_EQ(c[7].name, "shamans1");
    EXPECT_EQ(c[8].kind, ScriptCommandKind::ExpectAlive);
}

TEST(RunScript, ReadsTheCommandsOfAFullRun)
{
    RunScriptResult const result = ParseRunScript(Head +
        "route\nroute noquests\nfight pack1\nfight pack2 200\nwait rested\nwait rested 90\nrelease 0\nrelease never\n");
    ASSERT_TRUE(result.ok) << result.error;

    std::vector<ScriptCommand> const& c = result.script.scenarios[0].commands;
    ASSERT_EQ(c.size(), 8u);
    EXPECT_EQ(c[0].kind, ScriptCommandKind::Route);
    EXPECT_EQ(c[0].quests, Bots::RunQuests::Do);
    EXPECT_EQ(c[1].kind, ScriptCommandKind::Route);
    EXPECT_EQ(c[1].quests, Bots::RunQuests::Skip);
    EXPECT_EQ(c[2].kind, ScriptCommandKind::Fight);
    EXPECT_EQ(c[2].name, "pack1");
    EXPECT_EQ(c[2].seconds, Bots::DefaultCombatEndSeconds);
    EXPECT_EQ(c[3].seconds, 200u);
    EXPECT_EQ(c[4].kind, ScriptCommandKind::WaitRested);
    EXPECT_EQ(c[4].seconds, Bots::DefaultRestedSeconds);
    EXPECT_EQ(c[5].seconds, 90u);
    EXPECT_EQ(c[6].kind, ScriptCommandKind::Release);
    EXPECT_EQ(c[6].seconds, 0u);
    EXPECT_FALSE(c[6].never);
    EXPECT_EQ(c[7].kind, ScriptCommandKind::Release);
    EXPECT_TRUE(c[7].never);
}

TEST(RunScript, RefusesABrokenLineOfAFullRun)
{
    EXPECT_EQ(ErrorOf(Head + "route all\n"), "line 6: use: route [noquests]");
    EXPECT_EQ(ErrorOf(Head + "fight\n"), "line 6: use: fight <pack> [<seconds>]");
    EXPECT_EQ(ErrorOf(Head + "release soon\n"), "line 6: use: release <seconds> | release never");
    EXPECT_EQ(ErrorOf(Head + "wait rested x\n"),
        "line 6: use: wait <seconds> | wait arrive | wait combat_end [<seconds>] | wait rested [<seconds>]");
}

namespace
{
    char const* const QuestRouteText =
        "route ragefire_chasm 389\n"
        "boss 11520\n"
        "quest 5722\n"
        "visit q5722_maur\n"
        "point p1 1 2 3 2\n"
        "point pull_pack1 4 5 6 2\n"
        "pack pack1 7 8 9\n"
        "mob 1 11320\n"
        "point q5722_maur 10 11 12 2 quest 5722\n"
        "pack pack2 13 14 15 quest 5722\n"
        "mob 2 11320\n"
        "pack boss 16 17 18\n"
        "mob 3 11520\n";

    std::vector<std::string> Sources(std::vector<ScriptCommand> const& commands)
    {
        std::vector<std::string> sources;
        for (ScriptCommand const& c : commands)
            sources.push_back(c.source);
        return sources;
    }
}

TEST(RunScript, ARouteLineWalksEachStepOfTheRoute)
{
    RunScriptResult const script = ParseRunScript(Head + "teleport p1\nroute\nexpect alive\n");
    Bots::RouteResult const route = Bots::ParseRoute(QuestRouteText);
    ASSERT_TRUE(script.ok && route.ok) << script.error << route.error;

    std::vector<ScriptCommand> const c = Bots::ExpandRoute(script.script.scenarios[0].commands, route.route);
    EXPECT_EQ(Sources(c), (std::vector<std::string>{
        "teleport p1",
        "move p1", "wait arrive",
        "move pull_pack1", "wait arrive",
        "wait rested", "command stay", "fight pack1", "wait rested", "command follow",
        "move q5722_maur", "wait arrive",
        "wait rested", "command stay", "fight pack2", "wait rested", "command follow",
        "wait rested", "command stay", "fight boss", "wait rested", "command follow",
        "expect alive"}));
    // Each step keeps the line of the route line, for the log and the result.
    EXPECT_EQ(c[1].line, 7u);
    EXPECT_EQ(c[1].kind, ScriptCommandKind::Move);
    EXPECT_EQ(c[1].name, "p1");
    EXPECT_EQ(c[2].kind, ScriptCommandKind::WaitArrive);
    EXPECT_EQ(c[5].kind, ScriptCommandKind::WaitRested);
    EXPECT_EQ(c[5].seconds, Bots::DefaultRestedSeconds);
    // The group holds its place while the tank pulls, as in the scenarios.
    EXPECT_EQ(c[6].kind, ScriptCommandKind::Command);
    EXPECT_EQ(c[6].text, "stay");
    EXPECT_EQ(c[7].kind, ScriptCommandKind::Fight);
    EXPECT_EQ(c[7].name, "pack1");
    EXPECT_EQ(c[7].seconds, Bots::DefaultCombatEndSeconds);
    // The group, the test driver too, rests after the fight and before the
    // next `move` (jvbastel/WOWAI#99).
    EXPECT_EQ(c[8].kind, ScriptCommandKind::WaitRested);
    EXPECT_EQ(c[8].seconds, Bots::DefaultRestedSeconds);
    EXPECT_EQ(c[9].kind, ScriptCommandKind::Command);
    EXPECT_EQ(c[9].text, "follow");
}

TEST(RunScript, ARouteWithoutQuestsSkipsTheQuestSteps)
{
    RunScriptResult const script = ParseRunScript(Head + "route noquests\n");
    Bots::RouteResult const route = Bots::ParseRoute(QuestRouteText);
    ASSERT_TRUE(script.ok && route.ok) << script.error << route.error;

    EXPECT_EQ(Sources(Bots::ExpandRoute(script.script.scenarios[0].commands, route.route)), (std::vector<std::string>{
        "move p1", "wait arrive",
        "move pull_pack1", "wait arrive",
        "wait rested", "command stay", "fight pack1", "wait rested", "command follow",
        "wait rested", "command stay", "fight boss", "wait rested", "command follow"}));
}

TEST(RunScript, AScenarioWithARouteLineIsAFullRun)
{
    RunScriptResult const full = ParseRunScript(Head + "teleport p1\nroute\n");
    RunScriptResult const boss = ParseRunScript(Head + "teleport p1\nfight boss\n");
    ASSERT_TRUE(full.ok && boss.ok);

    EXPECT_TRUE(Bots::IsFullRun(full.script.scenarios[0]));
    EXPECT_FALSE(Bots::IsFullRun(boss.script.scenarios[0]));
}

TEST(RunScript, ChecksThePackOfAFight)
{
    Bots::RouteResult const route = Bots::ParseRoute(QuestRouteText);
    RunScriptResult const script = ParseRunScript(Head + "fight nopack\n");
    ASSERT_TRUE(script.ok && route.ok);

    EXPECT_EQ(Bots::CheckRunScript(script.script, route.route), "line 6: the route ragefire_chasm has no pack named nopack");
}

// The two-pack scenario of jvbastel/WOWAI#103 keeps two packs.
TEST(RunScript, AClearKeepsEachNamedPack)
{
    RunScriptResult const result = ParseRunScript(Head + "clear 30 shamans1 shamans2\nclear 20\n");
    ASSERT_TRUE(result.ok) << result.error;

    std::vector<ScriptCommand> const& c = result.script.scenarios[0].commands;
    EXPECT_EQ(c[0].yards, 30.0f);
    EXPECT_EQ(c[0].packs, (std::vector<std::string>{"shamans1", "shamans2"}));
    EXPECT_TRUE(c[1].packs.empty());

    Bots::RouteResult const route = Bots::ParseRoute(RouteText);
    ASSERT_TRUE(route.ok);
    EXPECT_EQ(Bots::CheckRunScript(result.script, route.route),
        "line 6: the route ragefire_chasm has no pack named shamans2");
}

TEST(RunScript, KeepsAllWordsOfAChatLine)
{
    RunScriptResult const result = ParseRunScript(Head + "command Grukk pull\nwhisper Hela how are you\n");
    ASSERT_TRUE(result.ok) << result.error;

    std::vector<ScriptCommand> const& c = result.script.scenarios[0].commands;
    EXPECT_EQ(c[0].text, "Grukk pull");
    EXPECT_EQ(c[1].name, "Hela");
    EXPECT_EQ(c[1].text, "how are you");
}

TEST(RunScript, RefusesABrokenLineWithItsNumber)
{
    EXPECT_EQ(ErrorOf(Head + "jump high\n"), "line 6: unknown command 'jump'");
    EXPECT_EQ(ErrorOf(Head + "teleport\n"), "line 6: use: teleport <point>");
    EXPECT_EQ(ErrorOf(Head + "wait soon\n"),
        "line 6: use: wait <seconds> | wait arrive | wait combat_end [<seconds>] | wait rested [<seconds>]");
    EXPECT_EQ(ErrorOf(Head + "expect boss_dead x\n"), "line 6: use: expect boss_dead <entry> | expect pack_dead <pack> | expect alive");
    EXPECT_EQ(ErrorOf(Head + "clear -3\n"), "line 6: use: clear <yards> [<pack> ...]");
    EXPECT_EQ(ErrorOf(Head + "whisper Hela\n"), "line 6: use: whisper <bot> <text>");
}

TEST(RunScript, NeedsTheHeadBeforeTheScenarios)
{
    EXPECT_EQ(ErrorOf("scenario s\nrefill\n"), "line 1: the script needs script, driver, group, and outside before its first scenario");
    EXPECT_EQ(ErrorOf("script rfc ragefire_chasm\ndriver Ezzra\ngroup Grukk\noutside 1 0 0 0\nrefill\n"),
        "line 5: a command needs a scenario line above it");
    EXPECT_EQ(ErrorOf(Head + "refill\nscenario s\nrefill\n"), "line 7: the scenario s is in the file already");
    EXPECT_EQ(ErrorOf(Head), "line 5: the scenario s has no commands");
}

TEST(RunScript, ChecksTheNamesAgainstTheRoute)
{
    RunScriptResult const script = ParseRunScript(Script);
    Bots::RouteResult const route = Bots::ParseRoute(RouteText);
    ASSERT_TRUE(script.ok && route.ok);

    EXPECT_EQ(Bots::CheckRunScript(script.script, route.route), "");

    RunScriptResult const wrong = ParseRunScript(Head + "move nowhere\ntarget nopack\nexpect pack_dead shaman_pull\n");
    ASSERT_TRUE(wrong.ok) << wrong.error;
    EXPECT_EQ(Bots::CheckRunScript(wrong.script, route.route), "line 6: the route ragefire_chasm has no point named nowhere");

    RunScriptResult const pack = ParseRunScript(Head + "expect pack_dead shaman_pull\n");
    EXPECT_EQ(Bots::CheckRunScript(pack.script, route.route), "line 6: the route ragefire_chasm has no pack named shaman_pull");
}

namespace
{
    std::string ReadDataFile(std::string const& path)
    {
        std::ifstream in(std::string(BOT_DATA_DIR) + "/" + path, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        EXPECT_TRUE(in.good() || in.eof()) << path;
        return text.str();
    }
}

TEST(RunScriptData, TheRagefireChasmScriptFitsItsRouteFile)
{
    RunScriptResult const script = ParseRunScript(ReadDataFile("bottests/ragefire_chasm.txt"));
    ASSERT_TRUE(script.ok) << script.error;
    Bots::RouteResult const route = Bots::ParseRoute(ReadDataFile("routes/" + script.script.route + ".txt"));
    ASSERT_TRUE(route.ok) << route.error;

    EXPECT_EQ(Bots::CheckRunScript(script.script, route.route), "");
    EXPECT_NE(Bots::FindScenario(script.script, "taragaman"), nullptr);
    EXPECT_NE(Bots::FindScenario(script.script, "enforcers"), nullptr);
    EXPECT_EQ(route.route.bosses, (std::vector<std::uint32_t>{11517, 11520, 11518, 11519}));
}

TEST(RunScriptData, TheFullRunsOfRagefireChasmFightEachBoss)
{
    RunScriptResult const script = ParseRunScript(ReadDataFile("bottests/ragefire_chasm.txt"));
    ASSERT_TRUE(script.ok) << script.error;
    Bots::RouteResult const route = Bots::ParseRoute(ReadDataFile("routes/" + script.script.route + ".txt"));
    ASSERT_TRUE(route.ok) << route.error;

    for (char const* name : {"full", "full_noquests"})
    {
        Bots::ScriptScenario const* full = Bots::FindScenario(script.script, name);
        ASSERT_NE(full, nullptr) << name;
        EXPECT_TRUE(Bots::IsFullRun(*full)) << name;
        std::set<std::uint32_t> fought;
        for (ScriptCommand const& c : Bots::ExpandRoute(full->commands, route.route))
            if (c.kind == ScriptCommandKind::Fight)
                for (Bots::RouteMob const& mob : Bots::FindPack(route.route, c.name)->mobs)
                    fought.insert(mob.entry);
        for (std::uint32_t boss : {11517u, 11520u, 11518u, 11519u})
            EXPECT_EQ(fought.count(boss), 1u) << name << " fights no pack with the boss " << boss;
    }
}

// jvbastel/WOWAI#118: the three-enemy scenario. One fixed pack of three
// melee mobs at about level 18 in the Deadmines, fought at level 18. The
// scenario has no ranged pull: the tank pulls with `fight`.
TEST(RunScriptData, TheDeadminesScriptFightsOnePackOfThreeMeleeMobsAtLevel18)
{
    RunScriptResult const script = ParseRunScript(ReadDataFile("bottests/deadmines.txt"));
    ASSERT_TRUE(script.ok) << script.error;
    Bots::RouteResult const route = Bots::ParseRoute(ReadDataFile("routes/" + script.script.route + ".txt"));
    ASSERT_TRUE(route.ok) << route.error;

    EXPECT_EQ(Bots::CheckRunScript(script.script, route.route), "");
    EXPECT_EQ(route.route.mapId, 36u);
    EXPECT_EQ(script.script.level, 18u);
    Bots::ScriptScenario const* three = Bots::FindScenario(script.script, "three_enemies");
    ASSERT_NE(three, nullptr);
    std::vector<std::string> fought;
    for (ScriptCommand const& c : three->commands)
        if (c.kind == ScriptCommandKind::Fight)
            fought.push_back(c.name);
    ASSERT_EQ(fought.size(), 1u);
    Bots::RoutePack const* pack = Bots::FindPack(route.route, fought[0]);
    ASSERT_NE(pack, nullptr);
    ASSERT_EQ(pack->mobs.size(), 3u);
    for (Bots::RouteMob const& mob : pack->mobs)
        EXPECT_EQ(mob.entry, 598u) << "each mob is a Defias Miner (level 17-18, melee)";
}

// jvbastel/WOWAI#118: the miners were too easy (today's tank lost no
// threat). The measured three-enemy pack: Rethilgore and two Bleak Worgs in
// Shadowfang Keep, three elite melee enemies, fought at level 18.
TEST(RunScriptData, TheShadowfangKeepScriptFightsRethilgoreAndHisTwoWorgsAtLevel18)
{
    RunScriptResult const script = ParseRunScript(ReadDataFile("bottests/shadowfang_keep.txt"));
    ASSERT_TRUE(script.ok) << script.error;
    Bots::RouteResult const route = Bots::ParseRoute(ReadDataFile("routes/" + script.script.route + ".txt"));
    ASSERT_TRUE(route.ok) << route.error;

    EXPECT_EQ(Bots::CheckRunScript(script.script, route.route), "");
    EXPECT_EQ(route.route.mapId, 33u);
    EXPECT_EQ(script.script.level, 18u);
    Bots::ScriptScenario const* three = Bots::FindScenario(script.script, "three_enemies");
    ASSERT_NE(three, nullptr);
    std::vector<std::string> fought;
    for (ScriptCommand const& c : three->commands)
        if (c.kind == ScriptCommandKind::Fight)
            fought.push_back(c.name);
    ASSERT_EQ(fought, (std::vector<std::string>{"rethilgore"}));
    Bots::RoutePack const* pack = Bots::FindPack(route.route, "rethilgore");
    ASSERT_NE(pack, nullptr);
    std::multiset<std::uint32_t> entries;
    for (Bots::RouteMob const& mob : pack->mobs)
        entries.insert(mob.entry);
    EXPECT_EQ(entries, (std::multiset<std::uint32_t>{3861, 3861, 3914}));
}
