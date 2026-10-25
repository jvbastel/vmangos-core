#include "RunScript.h"
#include "Route.h"

#include <gtest/gtest.h>

#include <fstream>
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
    EXPECT_EQ(c[1].name, "shamans1");
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
    EXPECT_EQ(ErrorOf(Head + "wait soon\n"), "line 6: use: wait <seconds> | wait arrive | wait combat_end [<seconds>]");
    EXPECT_EQ(ErrorOf(Head + "expect boss_dead x\n"), "line 6: use: expect boss_dead <entry> | expect pack_dead <pack> | expect alive");
    EXPECT_EQ(ErrorOf(Head + "clear -3\n"), "line 6: use: clear <yards> [<pack>]");
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
