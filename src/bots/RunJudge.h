#ifndef WOWAI_BOTS_RUN_JUDGE_H
#define WOWAI_BOTS_RUN_JUDGE_H

#include "BotView.h"
#include "FightSpot.h"
#include "RunResult.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

// The end of a scripted run (spec section 10, Start and end of a run). The
// ScriptRunner knows the commands of a scenario; this judge knows the rules
// of the whole run:
// - A full run passes when the last boss of the route dies (the fourth one
//   in Ragefire Chasm).
// - A run fails at the second wipe, after 70 minutes, or after 3 minutes
//   with no progress: no kill, no damage dealt, and no movement along the
//   route. (A lost mob fails it too: bots/ThreatWatch.h.)
// - A threat loss never fails it, on the healer or not: the judge does not
//   see them. The result counts them (RunResult.h).
// - An extra pack fails its pull when it had no warn, or when a better pull
//   could have kept it out (jvbastel/WOWAI#123, below). A failed pull is
//   counted and shown in the result, but it does not fail the run (user
//   decision, 2026-10-27).
// The game layer tells the judge the facts, then asks for the verdict.
namespace Bots
{
    std::int64_t const RunLimitMs = 70 * 60000;
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

    // A pack of the route round a pull: its name and its living enemies.
    struct RoutePackAtPull
    {
        std::string name;
        std::vector<Guid> enemies;
    };

    // One pull that the tank could have made, and the route packs it
    // brings besides the pull's own.
    struct PullChoice
    {
        std::string way; // "a fight spot at X on the cleared way", ...
        std::set<std::string> brings;
    };

    // The pulls of `target` that the tank could have made, as the judge
    // sees them at the pull, in route packs.
    struct PullChoices
    {
        // The route packs that are one pack with the target
        // (SplitTargetPack, FightSpot.h): no pull keeps them out.
        std::set<std::string> withTarget;
        // The route packs that the search saw.
        std::set<std::string> searched;
        std::vector<PullChoice> choices;
    };

    // The rules of FightSpot.h again (FindPullOptions), with all the real
    // packs as `tank` sees them at the pull and its cleared way `trail`
    // (GroupCoordinator::GetTrail). `packs`: the route packs round the pull
    // but its own. A pack of the search (SplitIntoPacks) counts as the route
    // packs of its enemies: so a pull brings route packs, as the run counts
    // them (ExtraPackWatch.h).
    PullChoices FindPullChoices(BotView const& tank, std::vector<Position> const& trail, Guid target,
        std::vector<RoutePackAtPull> const& packs);

    // The mark of route pack `pack` of the pull, when the pull brought `got`
    // extra packs in all: avoidable when a pull of `choices` kept it out and
    // brought no more than `got` extra packs.
    ExtraPackMark MarkExtraPack(PullChoices const& choices, std::string const& pack, std::size_t got);

    // An enemy that fled in the pull and is this near to an enemy of a pack
    // when the pack joins brought it: twice the assistance radius
    // (FightSpot.h), for the flee and the call for help at its end.
    float const FleeBringsYards = 2 * AssistanceYards;

    // What the game layer saw bring the extra pack when it joined.
    struct ExtraPackCause
    {
        // An enemy of the pull that fled (the flee event of RunTrace.h) and
        // came near the pack, or "".
        std::string fleeing;
        // An enemy of the pack patrols (jvbastel/WOWAI#108): its spawn walks
        // waypoints, or follows a leader who does.
        bool patrol = false;
        // A `join` of the script sent the pack into the fight (RunScript.h).
        bool scripted = false;
    };

    // The mark of an extra pack when it joins: `atPull`, the mark of
    // MarkExtraPack with the choices at the pull, or null when the pack was
    // out of reach of the search then. A `join`, a flee, or a patrol makes any
    // pack unavoidable.
    // `pullJudged` is false when the run could not mark the packs at the
    // pull (no living tank, no target): then a pack with no flee or patrol
    // is "not judged", and not avoidable. A pack sent by the script's `join`
    // command (`cause.scripted`) is unavoidable all the same.
    ExtraPackMark JudgeExtraPack(bool pullJudged, ExtraPackMark const* atPull, ExtraPackCause const& cause);

    // Why the pull fails: its first unwarned or avoidable extra pack. Empty
    // when the pull passes. Each extra pack counts in the score of the run
    // (RunResult::pulls) all the same.
    std::string PullFailure(RunPull const& pull);
    // The number of pulls of a run that fail (PullFailure). They do not fail
    // the run.
    std::size_t FailedPulls(std::vector<RunPull> const& pulls);
}

#endif
