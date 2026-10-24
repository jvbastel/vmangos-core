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

    // Why a bot rests: it eats, drinks, or revives at its corpse.
    enum class RestReason
    {
        None, // The bot does not rest.
        Eat,
        Drink,
        Revive,
    };

    enum class GroupEventKind
    {
        Wipe, // Every group member is dead.
        Rest, // A bot starts to eat or drink, or revives at its corpse.
    };

    // A group event that the coordinator saw, for the bot chat events
    // `wipe` and `rest`.
    struct GroupEvent
    {
        GroupEventKind kind;
        Guid bot;          // The bot that rests. NoGuid for a wipe.
        RestReason reason; // None for a wipe.
    };

    // The only shared state between the bots of one group. The bot manager
    // keeps one per group. A bot without a group has one of its own, so that
    // the strategies always have a coordinator.
    //
    // Threads: the bots of one group can briefly sit on two map threads, so
    // each call takes a small mutex. A call returns a copy, never a reference
    // into the coordinator.
    //
    // Later tickets add the interrupt claims here.
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

        // The wipe: every group member is dead. The first SetWiped(true)
        // after a time with a living member adds a wipe event.
        bool IsWiped() const;
        void SetWiped(bool wiped);

        // The bots that eat or drink now, after a fight: the group waits for
        // them. A bot that starts to rest adds a rest event. RestReason::None:
        // the bot does not rest (any more).
        void SetResting(Guid bot, RestReason reason);
        std::vector<Guid> GetResting() const;
        // A bot revives at its corpse: adds a rest event.
        void NoteRevive(Guid bot);

        // The group events since the last call, oldest first. The bot
        // manager takes them on the world thread.
        std::vector<GroupEvent> TakeEvents();

    private:
        mutable std::mutex m_mutex;
        bool m_wiped = false;
        std::map<Guid, RestReason> m_resting;
        std::vector<GroupEvent> m_events;
        Guid m_focusTarget = NoGuid;
        PullState m_pull = {PullPhase::None, NoGuid, NoGuid};
    };
}

#endif
