#ifndef WOWAI_BOTS_RUN_SCRIPT_H
#define WOWAI_BOTS_RUN_SCRIPT_H

#include "BotRecipe.h"
#include "BotTypes.h"
#include "RouteQuests.h"

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
//   level 18                              # optional: the level of each member
//                                         # at the reset (default 16)
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
// - `clear <yards> [<pack> ...]`: the living creatures within the distance
//   of the test driver go away, except the bosses of the route and the mobs
//   of the packs.
// - `refill`: each group member gets full health and mana.
// - `wait <seconds>`, `wait arrive` (the test driver reached its point),
//   `wait combat_end [<seconds>]` (a fight starts, then the group is out of
//   combat; the run fails after the seconds).
// - `expect boss_dead <entry>`, `expect pack_dead <pack>`, `expect alive`
//   (each group member lives). A failed expect ends the run as failed.
// - `wait rested [<seconds>]`: each member lives, and no bot of the group
//   eats or drinks any more; the run fails after the seconds.
// - `fight <pack> [<seconds>]`: the test driver selects a living mob of the
//   pack and says `pull` in party chat, then the run waits for the end of
//   the fight, as `wait combat_end`. A pack that is dead already is no fight:
//   the run goes on at once. A pack that lives after its fight (the tank
//   died, the pack reset) is fought again at most twice: the group lives
//   and rests, follows the driver back over the route points from the one
//   nearest to it to the last one before the pack (Route.h,
//   WalkBackPoints), holds its place when it did so before, and the driver
//   pulls again. Then the run fails.
// - `join <pack> [<bot>]`: the living mobs of the pack attack a group member
//   at once, as a call for help makes them: they come from where they
//   stand, for example into a fight that a `command pull` started (a "name
//   pull"). `<bot>` is the member they go for; with none, the living member
//   nearest to the pack. The result lists the pack as an extra pack of the
//   pull that the script brought, so it does not count against the tank.
// - `release <seconds>` or `release never`: in a wipe, the test driver
//   releases this long after the last member died (default 5 s), and the
//   group revives at the dungeon entrance. `never`: nobody releases.
// - `route [noquests]`: the whole route, step by step (a full run). Each
//   point is `move <point>` and `wait arrive`. Each pack is `wait rested`,
//   `command stay`, `fight <pack>`, `wait rested`, and `command follow`: the
//   group holds its place while the tank pulls, as in the scenarios, and the
//   group, the test driver too, rests before the next move. `noquests` leaves out the steps that are only there for a
//   quest. A scenario with a `route` line passes when the last boss of the
//   route dies.
namespace Bots
{
    // The longest fight of `wait combat_end` and `fight` without their own
    // number.
    std::uint32_t const DefaultCombatEndSeconds = 300;
    // The longest rest of `wait rested` without its own number.
    std::uint32_t const DefaultRestedSeconds = 120;

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
        WaitRested,
        Fight,
        Join,
        Release,
        Route,
        // Only the runner adds it, for a fight again: the walk of the group
        // back to the pack `name` over the route points (ScriptRunner).
        WalkBack,
    };

    struct ScriptCommand
    {
        ScriptCommandKind kind;
        // The point, the pack, or the whispered bot. Empty when unused.
        std::string name;
        // The chat text of `command` and `whisper`.
        std::string text;
        // The member that the pack of `join` goes for, or empty: the member
        // nearest to the pack.
        std::string victim;
        // The creature entry of `target <entry>` and `expect boss_dead`.
        std::uint32_t entry = 0;
        // The distance of `clear`, and the packs that stay.
        float yards = 0.0f;
        std::vector<std::string> packs;
        // The time of `wait <seconds>` and `release`, and the limit of
        // `wait combat_end`, `wait rested`, and `fight`.
        std::uint32_t seconds = 0;
        // `release never`.
        bool never = false;
        // `route` or `route noquests`.
        RunQuests quests = RunQuests::Do;
        // A `fight` that the runner added: 1 for the first fight again of
        // the pack, and so on. 0 for the fight of the script.
        std::uint32_t again = 0;
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
        // The level of the test driver and the bots at the reset of a run
        // (`level <n>`, jvbastel/WOWAI#118).
        std::uint32_t level = Version1BotLevel;
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

    // Empty when each point and pack that the script names is in the route,
    // else a short line for the GM.
    std::string CheckRunScript(RunScript const& script, Route const& route);

    // The commands with each `route` line replaced by the steps of the route
    // that the run walks. Each step keeps the line of its `route` line.
    std::vector<ScriptCommand> ExpandRoute(std::vector<ScriptCommand> const& commands, Route const& route);

    // A scenario with a `route` line: a full run of the dungeon.
    bool IsFullRun(ScriptScenario const& scenario);
}

#endif
