#ifndef WOWAI_BOTS_RUN_RESULT_H
#define WOWAI_BOTS_RUN_RESULT_H

#include "BotRecipe.h"
#include "BotTypes.h"
#include "FightWatch.h"
#include "RouteQuests.h"

#include <cstdint>
#include <string>
#include <vector>

// The result file of a scripted run (spec section 10, Results): one JSON
// file per run, `<script>-<scenario>-<stamp>.json` in the run folder. The
// game layer fills a RunResult as the run goes; all times are run times in
// milliseconds, and the file writes them in seconds.
namespace Bots
{
    enum class RunOutcome
    {
        Passed,
        Failed,
        Aborted, // A GM stopped the run.
    };

    struct RunBossKill
    {
        std::uint32_t entry;
        std::string name;
        std::int64_t ms;
    };

    struct RunDeath
    {
        std::string member;
        std::int64_t ms;
    };

    // A pack of the route that joined the fight of a pull, but is not the
    // pack of the pull (jvbastel/WOWAI#103, bots/ExtraPackWatch.h).
    struct RunExtraPack
    {
        std::string pack;
        std::int64_t ms = 0; // When the group first fought a mob of it.
        bool warned = false; // The tank warned in party chat in that pull.
    };

    // A `fight` of a pack, or a `command pull` of a scenario (no pack).
    struct RunPull
    {
        std::string pack;   // Empty for a `command pull`.
        std::string target; // The unit that the test driver selected.
        std::int64_t ms;
        bool again = false; // A fight again of a pack that lived after its fight.
        std::vector<RunExtraPack> extraPacks;
    };

    // A threat loss or a lost mob: the enemy hit the member from start to end.
    struct RunHit
    {
        std::string enemy;
        std::string member;
        std::int64_t startMs;
        std::int64_t endMs;
    };

    // An interrupt that landed: the cast `spell` of the enemy stopped.
    struct RunInterrupt
    {
        std::string member;
        std::string enemy;
        std::uint32_t spell;
        std::string spellName;
        CastKind kind;
        std::int64_t ms;
    };

    // A cast that ended with no interrupt while a member could interrupt
    // (bots/InterruptWatch.h). `ms` is the start of the cast.
    struct RunMissedCast
    {
        std::string enemy;
        std::uint32_t spell;
        std::string spellName;
        CastKind kind;
        std::int64_t ms;
    };

    // `second` tried to interrupt the cast that `first` interrupted.
    struct RunDoubleInterrupt
    {
        std::string enemy;
        std::uint32_t spell;
        std::string spellName;
        std::string first;
        std::string second;
        std::int64_t ms;
    };

    // A chase beyond the limit (bots/ChaseWatch.h).
    struct RunChase
    {
        std::string member;
        std::string enemy;
        std::int64_t startMs;
        std::int64_t endMs;
        float maxYards;
    };

    // A Fire Nova of Taragaman that hit a member.
    struct RunFireNovaHit
    {
        std::string member;
        BotRole role;        // The test driver counts as Ranged.
        std::uint32_t fight; // FightNumber at the hit.
        std::uint32_t damage;
        std::int64_t ms;
    };

    // A line of a bot that arrived in game chat.
    struct RunChatLine
    {
        std::string bot;
        std::int64_t ms;
    };

    // A bot chat event of the run group that the core sent to the chat
    // service, and the lines of its answer that the bots said in game.
    struct RunChatEvent
    {
        std::string event; // The event name of the contract: boss_kill, wipe, ...
        std::string to;    // The whispered bot of a player_message, or empty.
        std::int64_t ms;
        bool reached = false; // The chat service answered it.
        std::vector<RunChatLine> lines;
    };

    struct RunGearItem
    {
        std::string position;
        std::uint32_t entry;
        std::string name;
    };

    struct RunGearDraw
    {
        std::string member;
        std::string gearTemplate;
        std::string shape;
        std::vector<RunGearItem> items;
    };

    // A random place of a route point.
    struct RunChoice
    {
        std::string who;
        std::string point;
        std::uint32_t place; // 1 for the first place of the point.
        std::uint32_t places;
        float x;
        float y;
        std::int64_t ms;
    };

    struct RunResult
    {
        std::uint32_t number = 0; // The runs since the start of mangosd.
        std::string script;
        std::string scenario;
        bool fullRun = false;
        RunQuests quests = RunQuests::Do; // Of a full run.
        std::string startedAt;            // UTC, ISO 8601.
        std::string stamp;                // UTC, YYYYMMDD-HHMMSS, for the file names.
        std::int64_t durationMs = 0;
        RunOutcome outcome = RunOutcome::Failed;
        std::string reason; // Empty for a passed run.
        std::string driver;
        std::vector<std::string> group;
        std::vector<RunBossKill> bossKills;
        std::vector<std::int64_t> wipes;
        std::vector<RunDeath> deaths;
        std::vector<RunPull> pulls;
        std::vector<FightSummary> fights; // Who took part in each fight that ended.
        std::vector<RunHit> threatLosses;
        // The members with the healer role, for the healer's threat-loss
        // count (jvbastel/WOWAI#113).
        std::vector<std::string> healers;
        std::vector<RunHit> lostMobs;
        // The scripted moves of the test driver that stopped before they
        // arrived (ScriptedMove.h).
        std::uint32_t movesStopped = 0;
        std::vector<RunInterrupt> interrupts;
        std::vector<RunMissedCast> missedCasts;
        std::vector<RunDoubleInterrupt> doubleInterrupts;
        std::vector<RunChase> chases;
        float chaseLimitYards = 0.0f; // The limit of `chases` (ChaseLimitYards).
        std::vector<RunFireNovaHit> fireNovaHits;
        std::vector<RunChatEvent> chatEvents;
        std::vector<RunGearDraw> gearDraws;
        std::vector<RunChoice> choices;
        std::vector<ObjectiveProgress> questProgress;
        std::string traceFile; // Empty when the run saved no trace.
    };

    // `<script>-<scenario>-<stamp>`, the name of the result file and the
    // trace without the extension.
    std::string RunFileStem(RunResult const& result);

    std::string RunResultJson(RunResult const& result);

    // The number of the fight of the last pull: 1, and one more for each
    // pull again (RunPull::again) of the same fight. 1 without a pull.
    std::uint32_t FightNumber(std::vector<RunPull> const& pulls);
}

#endif
