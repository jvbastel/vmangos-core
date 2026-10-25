#ifndef WOWAI_BOTS_RUN_SCRIPT_H
#define WOWAI_BOTS_RUN_SCRIPT_H

#include "BotTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The script file of the scripted runs of one dungeon, for example
// bots/data/bottests/ragefire_chasm.txt. One generic runner reads every
// script: a new dungeon needs a new file, not a new build. A script file
// looks like this:
//
//   script ragefire_chasm ragefire_chasm  # script name, route file name
//   driver Ezzra                          # the test driver (a frost Mage bot)
//   group Grukk Hela Shanka Morzul        # the four bots
//   outside 1 1811.2 -4410.5 -18.5        # map x y z: where the group waits
//                                         # between runs, out of the dungeon
//   scenario taragaman                    # one scenario, up to the next one
//   teleport taragaman_camp               # the group goes to a route point
//   clear 45                              # trash within 45 yd goes away
//   command follow                        # party chat of the test driver
//   wait 20
//   refill                                # full health and mana
//   target 11520                          # the driver selects Taragaman
//   command pull
//   wait combat_end 240
//   expect boss_dead 11520
//
// The commands:
// - `teleport <point>`: the test driver and the group go to the point.
// - `move <point>`: the test driver walks to the point. The bots that follow
//   it come along.
// - `command <text>`: the test driver says the text in party chat.
// - `whisper <bot> <text>`: the test driver whispers the text to a bot.
// - `target <entry>` or `target <pack>`: the test driver selects the nearest
//   living creature of the entry, or a living mob of the pack.
// - `clear <yards> [<pack>]`: the living creatures within the distance of
//   the test driver go away, except the bosses of the route and the mobs of
//   the pack.
// - `refill`: each group member gets full health and mana.
// - `wait <seconds>`, `wait arrive` (the test driver reached its point),
//   `wait combat_end [<seconds>]` (a fight starts, then the group is out of
//   combat; the run fails after the seconds).
// - `expect boss_dead <entry>`, `expect pack_dead <pack>`, `expect alive`
//   (each group member lives). A failed expect ends the run as failed.
namespace Bots
{
    // The longest fight of `wait combat_end` without its own number.
    std::uint32_t const DefaultCombatEndSeconds = 300;

    enum class ScriptCommandKind
    {
        Teleport,
        Move,
        Command,
        Whisper,
        Target,
        Clear,
        Refill,
        WaitSeconds,
        WaitArrive,
        WaitCombatEnd,
        ExpectBossDead,
        ExpectPackDead,
        ExpectAlive,
    };

    struct ScriptCommand
    {
        ScriptCommandKind kind;
        // The point, the pack, or the whispered bot. Empty when unused.
        std::string name;
        // The chat text of `command` and `whisper`.
        std::string text;
        // The creature entry of `target <entry>` and `expect boss_dead`.
        std::uint32_t entry = 0;
        // The distance of `clear`.
        float yards = 0.0f;
        // The time of `wait <seconds>`, and the limit of `wait combat_end`.
        std::uint32_t seconds = 0;
        // The line in the file and its words, for the log and the result.
        std::size_t line = 0;
        std::string source;
    };

    struct ScriptScenario
    {
        std::string name;
        std::size_t line = 0;
        std::vector<ScriptCommand> commands;
    };

    struct RunScript
    {
        std::string name;
        std::string route;
        std::string driver;
        std::vector<std::string> group;
        std::uint32_t outsideMap = 0;
        Position outside = {0.0f, 0.0f, 0.0f};
        std::vector<ScriptScenario> scenarios;
    };

    struct RunScriptResult
    {
        bool ok;
        RunScript script;  // Only when ok.
        std::string error; // "line <n>: ..." when not ok.
    };

    // Reads a script file. It uses the shared tokenizer, so `#` starts a
    // comment.
    RunScriptResult ParseRunScript(std::string const& text);

    // The scenario of that name, or nullptr.
    ScriptScenario const* FindScenario(RunScript const& script, std::string const& name);

    struct Route;
    // Empty when each point and pack that the script names is in the route,
    // else a short line for the GM.
    std::string CheckRunScript(RunScript const& script, Route const& route);
}

#endif
