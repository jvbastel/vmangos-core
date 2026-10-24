#ifndef WOWAI_BOTS_GROUP_COORDINATOR_H
#define WOWAI_BOTS_GROUP_COORDINATOR_H

#include "BotTypes.h"

#include <map>
#include <mutex>
#include <vector>

namespace Bots
{
    enum class PullPhase
    {
        None,    // No pull.
        Pulling, // The puller goes for the target.
        Holding, // The target attacks the puller: the tank holds threat.
    };

    struct PullState
    {
        PullPhase phase;
        Guid puller; // The tank. NoGuid without a pull.
        Guid target; // NoGuid without a pull.
    };

    // A bot will interrupt the cast of spell `spell` by enemy `caster`.
    struct InterruptClaim
    {
        Guid caster;
        SpellId spell;
        Guid bot;
    };

    // The only shared state between the bots of one group. The bot manager
    // keeps one per group. A bot without a group has one of its own, so that
    // the strategies always have a coordinator.
    //
    // Threads: the bots of one group can briefly sit on two map threads, so
    // each call takes a small mutex. A call returns a copy, never a reference
    // into the coordinator.
    //
    // Later tickets add the group events here.
    class GroupCoordinator
    {
    public:
        // The focus target: the target of the tank. The damage bots attack it.
        Guid GetFocusTarget() const;
        void SetFocusTarget(Guid target);

        // The pull state. Version 1 pulls on the `pull` command only: the
        // tank starts the pull. The pull target becomes the focus target.
        PullState GetPull() const;
        void StartPull(Guid puller, Guid target);
        // The target attacks the puller now. Only the puller can call it.
        void HoldPull(Guid puller);
        // The fight of the pull ends: no pull and no focus target.
        void EndPull();
        // True when the tank holds threat on the focus target: the target of
        // the pull attacks the tank, or the tank fights back an enemy outside
        // a pull. The damage bots wait for it.
        bool TankHoldsThreat() const;

        // The interrupt claims (GroupRules.h): two bots never interrupt the
        // same cast. An enemy casts one spell at a time, so a claim of a
        // caster on another spell is of a cast that ended.
        // Claims the cast of `spell` by `caster` for `bot`. True when no
        // other bot holds the claim on that cast.
        bool ClaimInterrupt(Guid caster, SpellId spell, Guid bot);
        // The bot that holds the claim on the cast, or NoGuid.
        Guid GetInterruptClaim(Guid caster, SpellId spell) const;
        std::vector<InterruptClaim> GetInterruptClaims() const;
        // The cast of `caster` ended, or its bot gave it up.
        void ReleaseInterrupt(Guid caster);

        // The tank of the group, or NoGuid. The bot AI of the tank sets it
        // on each think. For the chase limit (GroupRules.h).
        Guid GetTank() const;
        void SetTank(Guid tank);

        // The enemy healers: the enemies that cast a heal in a fight. The
        // damage bots attack them first (GroupRules.h).
        void MarkEnemyHealer(Guid enemy);
        void ForgetEnemyHealer(Guid enemy);
        std::vector<Guid> GetEnemyHealers() const;

    private:
        mutable std::mutex m_mutex;
        Guid m_focusTarget = NoGuid;
        PullState m_pull = {PullPhase::None, NoGuid, NoGuid};
        std::map<Guid, InterruptClaim> m_interruptClaims; // By caster.
        Guid m_tank = NoGuid;
        std::vector<Guid> m_enemyHealers;                 // In mark order.
    };
}

#endif
