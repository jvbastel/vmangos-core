#ifndef WOWAI_BOTS_RUN_JUDGE_H
#define WOWAI_BOTS_RUN_JUDGE_H

#include <cstdint>
#include <set>
#include <string>
#include <vector>

// The end of a scripted run (spec section 10, Start and end of a run). The
// ScriptRunner knows the commands of a scenario; this judge knows the rules
// of the whole run:
// - A full run passes when the last boss of the route dies (the fourth one
//   in Ragefire Chasm).
// - A run fails at the second wipe, after 45 minutes, or after 3 minutes
//   with no progress: no kill, no damage dealt, and no movement along the
//   route. (A lost mob fails it too: bots/ThreatWatch.h.)
// - A threat loss never fails it, on the healer or not: the judge does not
//   see them. The result counts them (RunResult.h). A lost mob fails it
//   (BotRun, bots/ThreatWatch.h).
// The game layer tells the judge the facts, then asks for the verdict.
namespace Bots
{
    std::int64_t const RunLimitMs = 45 * 60000;
    std::int64_t const NoProgressMs = 3 * 60000;
    // The wipe that fails the run.
    std::uint32_t const FailingWipe = 2;

    enum class RunVerdictKind
    {
        Running,
        Passed,
        Failed,
    };

    struct RunVerdict
    {
        RunVerdictKind kind;
        std::string reason; // For Failed.
    };

    class RunJudge
    {
    public:
        // `bosses`: the boss list of the route for a full run, empty for a
        // scenario. The start counts as progress.
        RunJudge(std::vector<std::uint32_t> bosses, std::int64_t startMs);

        // A boss kill is progress too.
        void NoteBossKill(std::uint32_t entry, std::int64_t nowMs);
        void NoteWipe();
        // A kill, damage dealt, or movement along the route.
        void NoteProgress(std::int64_t nowMs);

        // The last boss kill wins over a limit of the same update.
        RunVerdict Judge(std::int64_t nowMs) const;
        // The verdict when each command of the scenario worked: a scenario
        // passes, a full run only when every boss of the route died.
        RunVerdict AtScriptEnd() const;

        std::uint32_t Wipes() const { return m_wipes; }

    private:
        std::vector<std::uint32_t> m_bosses;
        std::set<std::uint32_t> m_killed;
        std::int64_t m_startMs;
        std::int64_t m_progressMs;
        std::uint32_t m_wipes = 0;
    };
}

#endif
