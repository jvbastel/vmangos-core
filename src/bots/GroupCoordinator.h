#ifndef WOWAI_BOTS_GROUP_COORDINATOR_H
#define WOWAI_BOTS_GROUP_COORDINATOR_H

#include "BotTypes.h"

#include <mutex>

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

    // A fight of the group with a boss that has a boss reaction
    // (BossReactions.h).
    struct BossFight
    {
        Guid boss;           // NoGuid without a boss fight.
        std::uint32_t entry; // The creature entry of the boss, or 0.
    };

    // The only shared state between the bots of one group. The bot manager
    // keeps one per group. A bot without a group has one of its own, so that
    // the strategies always have a coordinator.
    //
    // Threads: the bots of one group can briefly sit on two map threads, so
    // each call takes a small mutex. A call returns a copy, never a reference
    // into the coordinator.
    //
    // Later tickets add the interrupt claims and the group events here.
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

        // The boss fight of the group. The coordinator holds the boss
        // reaction of the group: each bot pushes the reaction of the boss
        // fight onto its stack, and pops it when the fight ends
        // (BossReactionSlot).
        BossFight GetBossFight() const;
        // The group fights boss `boss` of creature entry `entry` now. False
        // when another boss fight is on: it stays.
        bool StartBossFight(Guid boss, std::uint32_t entry);
        // The fight with boss `boss` ends. Nothing for another boss.
        void EndBossFight(Guid boss);

    private:
        mutable std::mutex m_mutex;
        Guid m_focusTarget = NoGuid;
        PullState m_pull = {PullPhase::None, NoGuid, NoGuid};
        BossFight m_bossFight = {NoGuid, 0};
    };
}

#endif
