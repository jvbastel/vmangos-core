#include "RunScript.h"

#include "DataTokenizer.h"
#include "Route.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace Bots
{
    namespace
    {
        // The highest level of a vanilla character.
        std::uint32_t const MaxRunLevel = 60;

        bool ReadNumber(std::string const& word, std::uint32_t& value)
        {
            if (word.empty() || word.size() > 9)
                return false;
            for (char c : word)
                if (c < '0' || c > '9')
                    return false;
            value = static_cast<std::uint32_t>(std::strtoul(word.c_str(), nullptr, 10));
            return true;
        }

        bool ReadFloat(std::string const& word, float& value)
        {
            char* end = nullptr;
            errno = 0;
            float const number = std::strtof(word.c_str(), &end);
            if (word.empty() || *end != '\0' || errno != 0 || !std::isfinite(number))
                return false;
            value = number;
            return true;
        }

        std::string Join(std::vector<std::string> const& words, std::size_t first)
        {
            std::string text;
            for (std::size_t i = first; i < words.size(); ++i)
                text += (i == first ? "" : " ") + words[i];
            return text;
        }

        std::string Usage(std::string const& keyword)
        {
            if (keyword == "teleport")
                return "use: teleport <point>";
            if (keyword == "move")
                return "use: move <point>";
            if (keyword == "command")
                return "use: command <text>";
            if (keyword == "whisper")
                return "use: whisper <bot> <text>";
            if (keyword == "target")
                return "use: target <entry> | target <pack>";
            if (keyword == "clear")
                return "use: clear <yards> [<pack> ...]";
            if (keyword == "refill")
                return "use: refill";
            if (keyword == "wait")
                return "use: wait <seconds> | wait arrive | wait combat_end [<seconds>] | wait rested [<seconds>]";
            if (keyword == "fight")
                return "use: fight <pack> [<seconds>]";
            if (keyword == "join")
                return "use: join <pack> [<bot>]";
            if (keyword == "release")
                return "use: release <seconds> | release never";
            if (keyword == "route")
                return "use: route [noquests]";
            if (keyword == "expect")
                return "use: expect boss_dead <entry> | expect pack_dead <pack> | expect alive";
            if (keyword == "script")
                return "use: script <name> <route>";
            if (keyword == "driver")
                return "use: driver <name>";
            if (keyword == "group")
                return "use: group <bot> ...";
            if (keyword == "outside")
                return "use: outside <map> <x> <y> <z>";
            if (keyword == "level")
                return "use: level <1-60>";
            return "use: scenario <name>";
        }

        // One command line. False when the words do not fit the keyword.
        bool ReadCommand(std::vector<std::string> const& w, ScriptCommand& c)
        {
            std::string const& keyword = w[0];
            std::size_t const n = w.size();
            if (keyword == "teleport" || keyword == "move")
            {
                c.kind = keyword == "teleport" ? ScriptCommandKind::Teleport : ScriptCommandKind::Move;
                if (n != 2)
                    return false;
                c.name = w[1];
                return true;
            }
            if (keyword == "command")
            {
                c.kind = ScriptCommandKind::Command;
                c.text = Join(w, 1);
                return n >= 2;
            }
            if (keyword == "whisper")
            {
                c.kind = ScriptCommandKind::Whisper;
                if (n < 3)
                    return false;
                c.name = w[1];
                c.text = Join(w, 2);
                return true;
            }
            if (keyword == "target")
            {
                c.kind = ScriptCommandKind::Target;
                if (n != 2)
                    return false;
                if (!ReadNumber(w[1], c.entry))
                    c.name = w[1];
                return true;
            }
            if (keyword == "clear")
            {
                c.kind = ScriptCommandKind::Clear;
                if (n < 2 || !ReadFloat(w[1], c.yards) || c.yards <= 0.0f)
                    return false;
                c.packs.assign(w.begin() + 2, w.end());
                return true;
            }
            if (keyword == "refill")
            {
                c.kind = ScriptCommandKind::Refill;
                return n == 1;
            }
            if (keyword == "wait")
            {
                if (n == 2 && w[1] == "arrive")
                {
                    c.kind = ScriptCommandKind::WaitArrive;
                    return true;
                }
                if ((n == 2 || n == 3) && w[1] == "combat_end")
                {
                    c.kind = ScriptCommandKind::WaitCombatEnd;
                    c.seconds = DefaultCombatEndSeconds;
                    return n == 2 || (ReadNumber(w[2], c.seconds) && c.seconds > 0);
                }
                if ((n == 2 || n == 3) && w[1] == "rested")
                {
                    c.kind = ScriptCommandKind::WaitRested;
                    c.seconds = DefaultRestedSeconds;
                    return n == 2 || (ReadNumber(w[2], c.seconds) && c.seconds > 0);
                }
                c.kind = ScriptCommandKind::WaitSeconds;
                return n == 2 && ReadNumber(w[1], c.seconds);
            }
            if (keyword == "fight")
            {
                c.kind = ScriptCommandKind::Fight;
                c.seconds = DefaultCombatEndSeconds;
                if (n < 2 || n > 3)
                    return false;
                c.name = w[1];
                return n == 2 || (ReadNumber(w[2], c.seconds) && c.seconds > 0);
            }
            if (keyword == "join")
            {
                c.kind = ScriptCommandKind::Join;
                if (n < 2 || n > 3)
                    return false;
                c.name = w[1];
                if (n == 3)
                    c.victim = w[2];
                return true;
            }
            if (keyword == "release")
            {
                c.kind = ScriptCommandKind::Release;
                if (n != 2)
                    return false;
                c.never = w[1] == "never";
                return c.never || ReadNumber(w[1], c.seconds);
            }
            if (keyword == "route")
            {
                c.kind = ScriptCommandKind::Route;
                if (n == 2 && w[1] == "noquests")
                    c.quests = RunQuests::Skip;
                return n == 1 || c.quests == RunQuests::Skip;
            }
            if (keyword == "expect")
            {
                if (n == 2 && w[1] == "alive")
                {
                    c.kind = ScriptCommandKind::ExpectAlive;
                    return true;
                }
                if (n == 3 && w[1] == "boss_dead")
                {
                    c.kind = ScriptCommandKind::ExpectBossDead;
                    return ReadNumber(w[2], c.entry);
                }
                if (n == 3 && w[1] == "pack_dead")
                {
                    c.kind = ScriptCommandKind::ExpectPackDead;
                    c.name = w[2];
                    return true;
                }
                return false;
            }
            return false;
        }

        bool IsCommandKeyword(std::string const& word)
        {
            for (char const* keyword : {"teleport", "move", "command", "whisper", "target", "clear", "refill", "wait", "expect",
                     "fight", "join", "release", "route"})
                if (word == keyword)
                    return true;
            return false;
        }

        RunScriptResult Fail(std::size_t line, std::string const& message)
        {
            return {false, RunScript(), "line " + std::to_string(line) + ": " + message};
        }
    }

    RunScriptResult ParseRunScript(std::string const& text)
    {
        RunScript script;
        bool haveScript = false, haveDriver = false, haveOutside = false;
        for (DataLine const& line : TokenizeLines(text))
        {
            std::vector<std::string> const& w = line.words;
            std::string const& keyword = w[0];
            if (keyword == "script")
            {
                if (w.size() != 3)
                    return Fail(line.number, Usage(keyword));
                script.name = w[1];
                script.route = w[2];
                haveScript = true;
            }
            else if (keyword == "driver")
            {
                if (w.size() != 2)
                    return Fail(line.number, Usage(keyword));
                script.driver = w[1];
                haveDriver = true;
            }
            else if (keyword == "group")
            {
                if (w.size() < 2)
                    return Fail(line.number, Usage(keyword));
                script.group.assign(w.begin() + 1, w.end());
            }
            else if (keyword == "outside")
            {
                if (w.size() != 5 || !ReadNumber(w[1], script.outsideMap) || !ReadFloat(w[2], script.outside.x) ||
                    !ReadFloat(w[3], script.outside.y) || !ReadFloat(w[4], script.outside.z))
                    return Fail(line.number, Usage(keyword));
                haveOutside = true;
            }
            else if (keyword == "level")
            {
                if (w.size() != 2 || !ReadNumber(w[1], script.level) || script.level < 1 || script.level > MaxRunLevel)
                    return Fail(line.number, Usage(keyword));
            }
            else if (keyword == "scenario")
            {
                if (w.size() != 2)
                    return Fail(line.number, Usage(keyword));
                if (!haveScript || !haveDriver || script.group.empty() || !haveOutside)
                    return Fail(line.number, "the script needs script, driver, group, and outside before its first scenario");
                if (FindScenario(script, w[1]))
                    return Fail(line.number, "the scenario " + w[1] + " is in the file already");
                if (!script.scenarios.empty() && script.scenarios.back().commands.empty())
                    return Fail(script.scenarios.back().line, "the scenario " + script.scenarios.back().name + " has no commands");
                ScriptScenario scenario;
                scenario.name = w[1];
                scenario.line = line.number;
                script.scenarios.push_back(scenario);
            }
            else if (IsCommandKeyword(keyword))
            {
                if (script.scenarios.empty())
                    return Fail(line.number, "a command needs a scenario line above it");
                ScriptCommand command;
                command.line = line.number;
                command.source = Join(w, 0);
                if (!ReadCommand(w, command))
                    return Fail(line.number, Usage(keyword));
                script.scenarios.back().commands.push_back(command);
            }
            else
                return Fail(line.number, "unknown command '" + keyword + "'");
        }
        if (!script.scenarios.empty() && script.scenarios.back().commands.empty())
            return Fail(script.scenarios.back().line, "the scenario " + script.scenarios.back().name + " has no commands");
        if (script.scenarios.empty())
            return Fail(1, "the script has no scenario");
        return {true, script, ""};
    }

    ScriptScenario const* FindScenario(RunScript const& script, std::string const& name)
    {
        for (ScriptScenario const& scenario : script.scenarios)
            if (scenario.name == name)
                return &scenario;
        return nullptr;
    }

    std::string CheckRunScript(RunScript const& script, Route const& route)
    {
        for (ScriptScenario const& scenario : script.scenarios)
            for (ScriptCommand const& c : scenario.commands)
            {
                std::string const at = "line " + std::to_string(c.line) + ": the route " + route.name;
                bool const point = c.kind == ScriptCommandKind::Teleport || c.kind == ScriptCommandKind::Move;
                bool const pack = !c.name.empty() && (c.kind == ScriptCommandKind::Target ||
                    c.kind == ScriptCommandKind::ExpectPackDead || c.kind == ScriptCommandKind::Fight ||
                    c.kind == ScriptCommandKind::Join);
                if (point && !FindPoint(route, c.name))
                    return at + " has no point named " + c.name;
                if (pack && !FindPack(route, c.name))
                    return at + " has no pack named " + c.name;
                if (!c.victim.empty() && std::find(script.group.begin(), script.group.end(), c.victim) == script.group.end())
                    return "line " + std::to_string(c.line) + ": " + c.victim + " is not in the group";
                for (std::string const& kept : c.packs)
                    if (!FindPack(route, kept))
                        return at + " has no pack named " + kept;
            }
        return "";
    }

    std::vector<ScriptCommand> ExpandRoute(std::vector<ScriptCommand> const& commands, Route const& route)
    {
        std::vector<ScriptCommand> expanded;
        for (ScriptCommand const& c : commands)
        {
            if (c.kind != ScriptCommandKind::Route)
            {
                expanded.push_back(c);
                continue;
            }
            auto add = [&](ScriptCommandKind kind, std::string const& name, std::uint32_t seconds, std::string const& source)
            {
                ScriptCommand step;
                step.kind = kind;
                step.name = name;
                step.seconds = seconds;
                step.line = c.line;
                step.source = source;
                expanded.push_back(step);
            };
            auto say = [&](std::string const& text)
            {
                add(ScriptCommandKind::Command, "", 0, "command " + text);
                expanded.back().text = text;
            };
            for (RouteStep const& step : RunSteps(route, c.quests))
                if (step.kind == RouteStepKind::Point)
                {
                    std::string const& point = route.points[step.index].name;
                    add(ScriptCommandKind::Move, point, 0, "move " + point);
                    add(ScriptCommandKind::WaitArrive, "", 0, "wait arrive");
                }
                else
                {
                    std::string const& pack = route.packs[step.index].name;
                    add(ScriptCommandKind::WaitRested, "", DefaultRestedSeconds, "wait rested");
                    say("stay");
                    add(ScriptCommandKind::Fight, pack, DefaultCombatEndSeconds, "fight " + pack);
                    // The group, the test driver too, rests before the next
                    // `move`: a scripted move does not wait for a drink
                    // (jvbastel/WOWAI#99).
                    add(ScriptCommandKind::WaitRested, "", DefaultRestedSeconds, "wait rested");
                    say("follow");
                }
        }
        return expanded;
    }

    bool IsFullRun(ScriptScenario const& scenario)
    {
        for (ScriptCommand const& c : scenario.commands)
            if (c.kind == ScriptCommandKind::Route)
                return true;
        return false;
    }
}
