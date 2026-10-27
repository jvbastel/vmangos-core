#ifndef WOWAI_BOTS_GROUP_COORDINATOR_H
#define WOWAI_BOTS_GROUP_COORDINATOR_H

#include "BotTypes.h"
#include "Trail.h"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <utility>
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
        // The fight spot (FightSpot.h): where the puller holds the pack.
        // Set by the puller (SetPullSpot); cleared when it is back there.
        bool hasSpot = false;
        Position spot = {0, 0, 0};
        // Where the puller shoots the target from, when it cannot from
        // where it stood (SetShootSpot).
        bool hasShootSpot = false;
        Position shootSpot = {0, 0, 0};
        // The thinks in which the puller ordered its chase into shooting
        // range (CountRangeChase). A chase that just started does not move
        // yet: a stop counts only after a few thinks.
        std::uint32_t rangeChaseThinks = 0;
    };

    // A bot will interrupt the cast of spell `spell` by enemy `caster`.
    struct InterruptClaim
    {
        Guid caster;
        SpellId spell;
        Guid bot;
    };

    // A fight of the group with a boss that has a boss reaction
    // (BossReactions.h).
    struct BossFight
    {
        Guid boss;           // NoGuid without a boss fight.
        std::uint32_t entry; // The creature entry of the boss, or 0.
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
        Wipe,     // Every group member is dead.
        Rest,     // A bot starts to eat or drink, or revives at its corpse.
        BossKill, // The group killed a creature of the boss list.
        LootRoll, // A loot roll of the group ended with a winner.
    };

    // A group event that the coordinator saw, for the bot chat events
    // `wipe`, `rest`, `boss_kill`, and `loot_roll`.
    struct GroupEvent
    {
        GroupEventKind kind;
        Guid bot = NoGuid;                    // Rest: the bot that rests.
        RestReason reason = RestReason::None; // Rest: why.
        Guid target = NoGuid;                 // BossKill: the boss. LootRoll: the looted creature.
        std::uint32_t entry = 0;              // BossKill: the creature entry. LootRoll: the item entry.
        Guid winner = NoGuid;                 // LootRoll: the bot or player who won the item.
    };

    // Each bot of a group sees the same kill and the same roll end, at about
    // the same time. The coordinator keeps one event for them, and forgets a
    // kill or roll after this time, so that a respawned creature counts again.
    std::int64_t const GroupEventMemoryMs = 60 * 1000;

    // Tells if enemy `enemy` still lives, as the asking bot sees it.
    using EnemyLives = std::function<bool(Guid enemy)>;

    // The only shared state between the bots of one group. The bot manager
    // keeps one per group, and one for the group's bots in each other place
    // than the leader's (CoordinatorRegistry.h). A bot without a group has
    // one of its own, so that the strategies always have a coordinator.
    //
    // Threads: the bots of one group can briefly sit on two map threads, so
    // each call takes a small mutex. A call returns a copy, never a reference
    // into the coordinator.
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
        // The puller notes where it stands at the start of its pull, or
        // clears the spot when it is back there. Only the puller can.
        void SetPullSpot(Guid puller, Position spot);
        void ClearPullSpot(Guid puller);
        // The puller notes where it pulls from range, or clears it when no
        // shot goes from there. Only the puller can.
        void SetShootSpot(Guid puller, Position spot);
        void ClearShootSpot(Guid puller);
        // The puller orders its chase into shooting range in one more think.
        // Only the puller can.
        void CountRangeChase(Guid puller);
        // The fight of the pull ends: no pull and no focus target.
        void EndPull();
        // True when the tank holds threat on the focus target: the target of
        // the pull attacks the tank, or the tank fights back an enemy outside
        // a pull. The damage bots wait for it.
        bool TankHoldsThreat() const;
        // The tank cannot taunt enemy `enemy` off another member now: it
        // knows no Taunt, or the taunt cannot go out in range (cooldown). The
        // damage bots stop their wait for the taunt (DamageRules.h). A new
        // focus target or pull forgets it.
        void GiveUpTaunt(Guid enemy);
        bool IsTauntGivenUp(Guid enemy) const;

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

        // The tank's trail since the last fight (Trail.h): the cleared way of
        // its fight spot (FightSpot.h). The tank notes where it stands on
        // each think (WatchTrail); `fights`: it fights or is dead, so the
        // trail is empty.
        void KeepTrail(Position at, bool fights);
        std::vector<Position> GetTrail() const;

        // The enemy healers: the enemies that cast a heal in a fight. The
        // damage bots attack them first (GroupRules.h).
        void MarkEnemyHealer(Guid enemy);
        void ForgetEnemyHealer(Guid enemy);
        std::vector<Guid> GetEnemyHealers() const;

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

        // The wipe: every group member is dead. The first SetWiped(true)
        // after a time with a living member adds a wipe event.
        bool IsWiped() const;
        void SetWiped(bool wiped);

        // The bots that eat or drink now, after a fight: the group waits for
        // them. A bot that starts to rest adds a rest event. RestReason::None:
        // the bot does not rest (any more). GetResting also holds each bot
        // that must rest until full.
        void SetResting(Guid bot, RestReason reason);
        std::vector<Guid> GetResting() const;
        // A bot that a scripted run revived, at half health and mana: it
        // eats and drinks until full, and the group waits for it from now
        // on. WatchRest clears it when the bot is full.
        void SetRestUntilFull(Guid bot);
        bool RestsUntilFull(Guid bot) const;
        void ClearRestUntilFull(Guid bot);
        // Bot `bot` rests no more and is no longer waited for: it died, left
        // the group, or logged out. The game layer calls it for a bot of
        // GetResting that is not a living member in the world any more.
        void ForgetRest(Guid bot);
        // A dead bot releases to run back, because no resurrect comes:
        // adds a rest event.
        void NoteReleaseToRunBack(Guid bot);

        // The resurrect of dead bot `bot` does not come: the Priest found
        // no safe place to cast it from. The bot releases instead. A bot
        // that lives again clears it (ClearGivenUpResurrect). `blockers`:
        // the enemies that kept the Priest away. The give-up ends when one
        // of them dies (RetryResurrects); with none, it lasts.
        void GiveUpResurrect(Guid bot, std::vector<Guid> const& blockers = {});
        bool IsResurrectGivenUp(Guid bot) const;
        void ClearGivenUpResurrect(Guid bot);
        // Ends each give-up with a blocker for which `lives` is false.
        void RetryResurrects(EnemyLives const& lives);

        // A bot saw SMSG_PARTYKILLLOG with a victim of the boss list. The
        // first note of a kill adds a boss kill event. `nowMs`: any steady
        // clock in milliseconds.
        void NoteBossKill(Guid boss, std::uint32_t entry, std::int64_t nowMs);
        // A bot saw SMSG_LOOT_ROLL_WON. The first note of a roll (looted
        // creature and loot slot) adds a loot roll event.
        void NoteRollWon(Guid looted, std::uint32_t itemSlot, std::uint32_t itemEntry, Guid winner, std::int64_t nowMs);

        // The group events since the last call, oldest first. The bot
        // manager takes them on the world thread.
        std::vector<GroupEvent> TakeEvents();

    private:
        // True for the first note of `key` within GroupEventMemoryMs.
        bool FirstNote(std::pair<Guid, std::int64_t> key, std::int64_t nowMs);

        mutable std::mutex m_mutex;
        bool m_wiped = false;
        std::map<Guid, RestReason> m_resting;
        std::set<Guid> m_restUntilFull;
        std::map<Guid, std::vector<Guid>> m_givenUpResurrects; // Bot: blockers.
        std::vector<GroupEvent> m_events;
        // The kills and rolls that made an event, with the time of the event.
        std::map<std::pair<Guid, std::int64_t>, std::int64_t> m_seen;
        Guid m_focusTarget = NoGuid;
        Guid m_tauntGivenUp = NoGuid;
        PullState m_pull = {PullPhase::None, NoGuid, NoGuid};
        std::map<Guid, InterruptClaim> m_interruptClaims; // By caster.
        Guid m_tank = NoGuid;
        Trail m_trail;
        std::vector<Guid> m_enemyHealers;                 // In mark order.
        BossFight m_bossFight = {NoGuid, 0};
    };
}

#endif
