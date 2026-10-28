#ifndef WOWAI_BOTS_RUN_JUDGE_H
#define WOWAI_BOTS_RUN_JUDGE_H

#include "BotView.h"
#include "FightSpot.h"
#include "RunResult.h"
#include "RunScript.h"

#include <cstddef>
#include <cstdint>
#include <map>
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
// - A threat loss on a member who is not the healer never fails it. The
//   result counts them (RunResult.h). Threat losses on the healer fail it
//   only when the scenario limits them (`expect healer_threat_losses <n>`,
//   RunScript.h, jvbastel/WOWAI#167): one pull with more than <n> fails the
//   run, in a scenario and in a full run.
// - A damage bot that chases farther from the tank than the scenario allows
//   (`expect damage_chase_yards <yards>`, jvbastel/WOWAI#171) fails it.
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
        // scenario. The start counts as progress. `limits`: those of the
        // scenario's `expect` lines.
        RunJudge(std::vector<std::uint32_t> bosses, std::int64_t startMs, ScriptLimits limits = ScriptLimits());

        // A new pull (a `fight`, or a `command pull`): the pack, or the
        // target of a pull with no pack. Its healer threat losses count
        // from 0.
        void NotePull(std::string const& name);
        // A threat loss on the healer ("<enemy> on <member>", ThreatWatch.h)
        // in the last pull, or before the first one.
        void NoteHealerThreatLoss(std::string const& name);

        // A pass check of the run failed (jvbastel/WOWAI#168, below): the
        // run fails with the reason of the first one.
        void NoteFailedCheck(std::string const& reason);

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

        // jvbastel/WOWAI#171: bot `member` of role `role` chases enemy
        // `enemy` now, `yardsFromTank` from the living tank. A damage bot
        // (Melee, Ranged) beyond the scenario's `expect damage_chase_yards`
        // fails the run. The first one is the reason.
        void NoteChase(std::string const& member, BotRole role, std::string const& enemy, float yardsFromTank,
            std::int64_t nowMs);

    private:
        std::vector<std::uint32_t> m_bosses;
        ScriptLimits m_limits;
        std::size_t m_pulls = 0;
        std::string m_pull;                      // The name of the last pull.
        std::vector<std::string> m_healerLosses; // In the last pull.
        // The reason of the first pull over the limit, or empty.
        std::string m_overLimit;
        // The reason of the first failed check (NoteFailedCheck), or empty.
        std::string m_failedCheck;
        std::set<std::uint32_t> m_killed;
        std::int64_t m_startMs;
        std::int64_t m_progressMs;
        std::uint32_t m_wipes = 0;
        // jvbastel/WOWAI#171: the first damage bot chase beyond the limit,
        // or empty.
        std::string m_farChase;
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

// jvbastel/WOWAI#168: two pass checks of the tank. The game layer feeds them
// at each update of the fight; a failure goes to RunJudge::NoteFailedCheck.
namespace Bots
{
    // Whom an enemy of the fight attacks.
    enum class EnemyVictim
    {
        Healer,
        Tank,
        OtherMember,
    };

    // An enemy of the fight that attacks a group member, at one update.
    struct FightEnemySeen
    {
        Guid enemy;
        std::string name;
        // Of an extra pack: of no pack of the pull (ExtraPackWatch.h), such
        // as a pack that joined, or a pet of an enemy of one.
        bool extraPack;
        EnemyVictim victim;
        // jvbastel/WOWAI#172: in the tank's melee reach.
        bool inTankMelee = false;
        // jvbastel/WOWAI#174: it has really hit its victim (a swing or a
        // spell at it, RealHitGate), not only taken it as its victim.
        bool hitVictim = true;
    };

    // `expect healer_pickup_seconds <n>`: each extra-pack enemy that hits
    // the healer attacks the tank within <n> s of its first hit on the
    // healer (jvbastel/WOWAI#174: its first real hit, FightEnemySeen::hitVictim). One that died is no failure; one that attacks another
    // member at <n> s is.
    class HealerPickupCheck
    {
    public:
        explicit HealerPickupCheck(ScriptLimit limit = ScriptLimit());

        // The living enemies that attack a member now.
        void Observe(std::int64_t nowMs, std::vector<FightEnemySeen> const& enemies);
        // The reason of the first failure, or empty.
        std::string const& Failure() const { return m_failure; }

    private:
        ScriptLimit m_limit;
        std::map<Guid, std::int64_t> m_firstHitMs; // Not on the tank since.
        std::set<Guid> m_onTank;                   // Taken in time.
        std::string m_failure;
    };

    // `expect threat_targets_without_cast <n>`: the tank turns to a threat
    // target (an enemy other than the focus) and casts at least one ability
    // on it before it turns to another unit. One that died needs no cast.
    // A run with more than <n> such turns fails.
    class ThreatTargetCastCheck
    {
    public:
        explicit ThreatTargetCastCheck(ScriptLimit limit = ScriptLimit());

        // The tank cast a spell on `target` (it went out).
        void NoteCast(Guid target);
        // The tank's target now (NoGuid: none, no turn), with its name, the
        // focus, and the living enemies of the fight.
        void Observe(Guid target, std::string const& name, Guid focus, std::set<Guid> const& living);
        std::string const& Failure() const { return m_failure; }

    private:
        ScriptLimit m_limit;
        Guid m_target = NoGuid;
        std::string m_name;
        bool m_threatTarget = false;
        bool m_cast = false;
        // Casts on another unit than the target, before the turn to it.
        std::set<Guid> m_castsAhead;
        std::vector<std::string> m_misses;
        std::string m_failure;
    };
}

// jvbastel/WOWAI#172: the tank's threat on an extra-pack enemy that comes at
// it. The game layer feeds it like the checks above.
namespace Bots
{
    // `expect extra_pack_threat_seconds <n>`: each extra-pack enemy that
    // attacks the tank gets a Sunder Armor or Revenge from the tank within
    // <n> s of reaching the tank's melee reach; one from before (an early
    // visit, a pickup) counts too. The <n> s count from its FIRST reach and
    // do not start again when it leaves the tank and comes back
    // (jvbastel/WOWAI#174); it fails at any later update while it still
    // attacks a member. One that died is no failure.
    class ExtraPackThreatCheck
    {
    public:
        explicit ExtraPackThreatCheck(ScriptLimit limit = ScriptLimit());

        // A spell of the tank went out on `target`: `firstRank`, the first
        // rank of its chain.
        void NoteCast(Guid target, SpellId firstRank);
        // The living enemies that attack a member now (inTankMelee set).
        void Observe(std::int64_t nowMs, std::vector<FightEnemySeen> const& enemies);
        std::string const& Failure() const { return m_failure; }

    private:
        ScriptLimit m_limit;
        std::map<Guid, std::int64_t> m_inReachMs; // First on the tank in reach.
        std::set<Guid> m_threatCast;              // Got Sunder Armor or Revenge.
        std::string m_failure;
    };
}

#endif
