#ifndef WOWAI_BOTS_RUN_RESULT_H
#define WOWAI_BOTS_RUN_RESULT_H

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

    struct RunPull
    {
        std::string pack;
        std::int64_t ms;
    };

    // A threat loss or a lost mob: the enemy hit the member from start to end.
    struct RunHit
    {
        std::string enemy;
        std::string member;
        std::int64_t startMs;
        std::int64_t endMs;
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
        std::vector<RunHit> threatLosses;
        std::vector<RunHit> lostMobs;
        std::vector<RunGearDraw> gearDraws;
        std::vector<RunChoice> choices;
        std::vector<ObjectiveProgress> questProgress;
        std::string traceFile; // Empty when the run saved no trace.
    };

    // `<script>-<scenario>-<stamp>`, the name of the result file and the
    // trace without the extension.
    std::string RunFileStem(RunResult const& result);

    std::string RunResultJson(RunResult const& result);
}

#endif
