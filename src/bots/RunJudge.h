#ifndef WOWAI_BOTS_RUN_JUDGE_H
#define WOWAI_BOTS_RUN_JUDGE_H

#include "BotView.h"
#include "RunResult.h"

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
//   see them. The result counts them (RunResult.h).
// - An extra pack fails its pull when it had no warn, or when a better pull
//   could have kept it out (jvbastel/WOWAI#123, below). A failed pull fails
//   the run at its end.
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

    // Avoidable and unavoidable extra packs (jvbastel/WOWAI#123). An extra
    // pack (ExtraPackWatch.h) is unavoidable when no pull could have kept
    // it out: no fight spot on the cleared way, no line-of-sight pull, and
    // no clean area round a caster's spawn (FightSpot.h, #105 and #121), or
    // when a fleeing enemy or a patrol brought it. Else it is avoidable:
    // such a pull existed, and the tank did not take it.
    struct ExtraPackMark
    {
        bool avoidable = true;
        std::string reason;
    };

    // The rules of FightSpot.h again for the other pack `pack` (its enemies)
    // of the pull of `target`, with the real packs as `tank` sees them at the
    // pull and its cleared way `trail` (GroupCoordinator::GetTrail). Only the
    // target's pack and `pack` count: a spot that keeps `pack` out marks it
    // avoidable, whatever other packs that spot brings.
    ExtraPackMark MarkExtraPack(BotView const& tank, std::vector<Position> const& trail, Guid target,
        std::vector<Guid> const& pack);

    // What the game layer saw bring the extra pack when it joined.
    struct ExtraPackCause
    {
        // An enemy of the pull that fled (the flee event of RunTrace.h) and
        // came near the pack, or "".
        std::string fleeing;
        // An enemy of the pack patrols (jvbastel/WOWAI#108): its spawn walks
        // waypoints, or follows a leader who does.
        bool patrol = false;
    };

    // The mark of an extra pack when it joins: `atPull`, the mark of
    // MarkExtraPack at the pull, or null when the pack was out of reach of
    // the search then. A flee or a patrol makes any pack unavoidable.
    ExtraPackMark JudgeExtraPack(ExtraPackMark const* atPull, ExtraPackCause const& cause);

    // Why the pull fails: its first unwarned or avoidable extra pack. Empty
    // when the pull passes. Each extra pack counts in the score of the run
    // (RunResult::pulls) all the same.
    std::string PullFailure(RunPull const& pull);
    // The verdict of the pulls of a run: Failed at the first pull that
    // fails, Passed otherwise.
    RunVerdict JudgePulls(std::vector<RunPull> const& pulls);
}

#endif
