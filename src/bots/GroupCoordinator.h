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

    // What the puller said in party chat of its wait for the group
    // (MemberSpot.h).
    enum class GroupWaitSaid
    {
        Nothing,
        Waits,        // GroupWaitLine.
        PullsWithout, // PullWithoutLine: the wait ended.
    };

    struct PullState
    {
        PullPhase phase;
        Guid puller; // The tank. NoGuid without a pull.
        Guid target; // NoGuid without a pull.
        // The fight spot (FightSpot.h): where the puller holds the pack.
        // Set by the puller (SetFightSpot); cleared when it is back there.
        bool hasFightSpot = false;
        Position fightSpot = {0, 0, 0};
        // Where the puller shoots the target from, when it cannot from
        // where it stood (SetShootSpot).
        bool hasShootSpot = false;
        Position shootSpot = {0, 0, 0};
        // The thinks in a row in which the puller ordered its chase into
        // shooting range and stood still (CountRangeChase). A chase that
        // just started or found a new path does not move yet: a stop counts
        // only after a few thinks.
        std::uint32_t stillChaseThinks = 0;
        // The tank's wait for the group (MemberSpot.h): it starts when the
        // puller notes its fight spot (StartGroupWait). `waitSaid`: what the
        // puller said of it in party chat.
        std::int64_t waitFromMs = 0;
        GroupWaitSaid waitSaid = GroupWaitSaid::Nothing;
        // When the target first attacked the puller (HoldPull): the tank
        // engaged. The healer waits for the tank's threat from then
        // (jvbastel/WOWAI#111).
        std::int64_t engagedMs = 0;
        // When a member first saw the target in melee contact with the
        // puller, after the tank engaged (NoteFocusContact). The tank
        // holds threat HoldContactMs after it (jvbastel/WOWAI#127).
        bool hasContact = false;
        std::int64_t contactMs = 0;
        // The puller is back at its fight spot in the held pull: the rule
        // "back at the fight spot" cleared it (ClearFightSpot), or the pull
        // was held with no fight spot, so with no walk back
        // (jvbastel/WOWAI#127).
        bool backAtFightSpot = false;
        // The puller warned about extra packs in this pull
        // (NoteExtraPackWarn): it warns once a pull.
        bool extraPackWarned = false;
        // jvbastel/WOWAI#121: the puller goes in to a caster that stayed
        // away (GoIn). The keep-out rule (KeepOut.h) then lets it step
        // near other packs, after its warn.
        bool goesIn = false;
    };

    // The member spot of a bot in the pull (MemberSpot.h).
    struct MemberSpot
    {
        Guid bot = NoGuid;
        // The bot found a safe spot. Without one it stays out of the wait.
        bool found = false;
        Position spot = {0, 0, 0};
        // The bot stands at its spot.
        bool inPlace = false;
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
        std::uint32_t entry = 0;              // BossKill: the creature entry. Wipe: the boss fought, or 0. LootRoll: the item entry.
        Guid winner = NoGuid;                 // LootRoll: the bot or player who won the item.
    };

    // Each bot of a group sees the same kill and the same roll end, at about
    // the same time. The coordinator keeps one event for them, and forgets a
    // kill or roll after this time, so that a respawned creature counts again.
    std::int64_t const GroupEventMemoryMs = 60 * 1000;

    // jvbastel/WOWAI#170: a bot whose health is lower than at any time in
    // this many milliseconds before is losing health (NoteHealthFalls).
    std::int64_t const HealthFallMs = 2000;

    // The clocks of a held pull, both from the engage (PullState::engagedMs)
    // and both once the tank is back at its fight spot
    // (PullState::backAtFightSpot), in this order:
    // - jvbastel/WOWAI#121: a caster that stays away (it does not come to
    //   the tank, nor into melee) CasterComeMs after the engage gets the
    //   tank: it goes in to the caster's spawn area (WarriorModule.h).
    // - jvbastel/WOWAI#127: a held pull is the tank's (TankHoldsThreat)
    //   HoldContactMs after the target came into melee contact with the
    //   tank (a few swings and a Sunder Armor). A target that never comes
    //   into melee (a caster) is the tank's HoldLimitMs after the engage:
    //   the ranged pull, the run of the target to the tank and the contact
    //   delay fit in it.
    // CasterComeMs is the shorter: the damage bots never open on a caster
    // before the tank went in to it.
    std::int64_t const CasterComeMs = 6000;
    std::int64_t const HoldContactMs = 2000;
    std::int64_t const HoldLimitMs = 8000;
    static_assert(CasterComeMs < HoldLimitMs, "the tank goes in to a caster before the damage bots open on it");

    // Why the tank holds threat on the focus now (TankHoldsThreatBy).
    enum class ThreatHeld
    {
        No,
        OutsideAPull, // The tank fights back an enemy outside a pull.
        MeleeContact, // HoldContactMs after the melee contact.
        TimeLimit,    // HoldLimitMs after the engage.
    };

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
        // The target attacks the puller now, at `nowMs`: the tank engages.
        // Only the puller can call it.
        void HoldPull(Guid puller, std::int64_t nowMs = 0);
        // A member sees the target of the held pull in melee contact with
        // the puller at `nowMs`. The first note of a pull counts.
        void NoteFocusContact(std::int64_t nowMs);
        // The puller notes where it stands at the start of its pull, or
        // clears the spot when it is back there. Only the puller can.
        void SetFightSpot(Guid puller, Position spot);
        void ClearFightSpot(Guid puller);
        // The puller notes where it pulls from range, or clears it when no
        // shot goes from there. Only the puller can.
        void SetShootSpot(Guid puller, Position spot);
        void ClearShootSpot(Guid puller);
        // The puller orders its chase into shooting range in one more think,
        // and stands still or moves (which starts the count again). Only the
        // puller can.
        void CountRangeChase(Guid puller, bool moving);
        // The puller starts its wait for the group at `nowMs`, or notes what
        // it said of it. Only the puller can.
        void StartGroupWait(Guid puller, std::int64_t nowMs);
        void NoteGroupWaitSaid(Guid puller, GroupWaitSaid said);
        // The puller warned about extra packs (WarnExtraPacks). Only the
        // puller can.
        void NoteExtraPackWarn(Guid puller);
        // The puller goes in to a caster that stayed away (PullState::goesIn).
        // Only the puller can.
        void GoIn(Guid puller);
        // The member spots of the pull that is on: each bot claims its own
        // spot (`found` false: none is safe), and then notes that it is in place;
        // it stays in place for the rest of the pull.
        // A new pull or the end of the pull forgets them.
        // Bot `bot` takes its member spot, so that two bots never take one
        // spot: when it has none yet, `find` runs with the pull and the
        // spots that the other members found, and its answer (a spot, or
        // false for none) is noted. `find` runs outside the lock (it asks
        // the game for paths and sight); when another bot took a spot, or a
        // new pull began meanwhile, it runs again with the new state.
        // Returns the bot's member spot.
        using MemberSpotFinder =
            std::function<bool(PullState const& pull, std::vector<Position> const& taken, Position& spot)>;
        MemberSpot ClaimMemberSpot(Guid bot, MemberSpotFinder const& find);
        void SetMemberInPlace(Guid bot);
        std::vector<MemberSpot> GetMemberSpots() const;
        // The bots of the group: each bot notes itself on each think
        // (NoteForPulls, MemberSpot.h) at `nowMs`, and whether it joins a
        // pull now, so that the tank knows whom to wait for. A player is no
        // bot. GetPullMembers: the bots that join pulls, noted within
        // `maxAgeMs` before `nowMs`.
        void NoteBot(Guid bot, std::int64_t nowMs, bool joinsPulls);
        std::vector<Guid> GetPullMembers(std::int64_t nowMs, std::int64_t maxAgeMs) const;
        // The fight of the pull ends: no pull and no focus target.
        void EndPull();
        // True at `nowMs` when the tank holds threat on the focus target:
        // the tank fights back an enemy outside a pull; or in a held pull,
        // the tank is back at its fight spot and HoldContactMs passed since
        // the melee contact (NoteFocusContact), or HoldLimitMs since the tank
        // engaged. The damage bots wait for it (DamageRules.h), and the
        // members hold their member spots until it (MemberSpot.h).
        bool TankHoldsThreat(std::int64_t nowMs) const;
        // The same, with the reason: melee contact before the time limit
        // when both hold. The trace of the hold says it (HoldWatch.h).
        ThreatHeld TankHoldsThreatBy(std::int64_t nowMs) const;
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
        // The healer of the group, or NoGuid. The bot AI of the healer sets
        // it on each think. The tank taunts enemies off it first
        // (WarriorModule.cpp).
        Guid GetHealer() const;
        void SetHealer(Guid healer);

        // The tank's trail since the last fight (Trail.h): the cleared way of
        // its fight spot (FightSpot.h). The tank notes where it stands on
        // each think (WatchTrail); `fights`: it fights or is dead, so the
        // trail is empty. While a pull is on, the trail does not grow.
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

        // The boss of the boss list (BotView::IsListedBoss) that the group
        // fights or fought last, for the boss name of a wipe. Any boss, with
        // or without a boss reaction (GetBossFight). {NoGuid, 0}: none.
        BossFight GetBossFought() const;
        // Sets the boss fought to `next` if it is still `expected`, under one
        // lock: two bots of the group can watch at the same time. False when
        // another bot changed it first.
        bool ReplaceBossFought(BossFight expected, BossFight next);

        // The wipe: every group member is dead. The first SetWiped(true)
        // after a time with a living member adds a wipe event, with the
        // entry of the boss fought (GetBossFought).
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

        // jvbastel/WOWAI#170: the health of bot `bot` at `nowMs` (its own
        // note, each think). True when it is below the highest health
        // noted in the HealthFallMs before: the bot is losing health.
        bool NoteHealthFalls(Guid bot, std::uint32_t health, std::int64_t nowMs);

    private:
        // A pull is on, and `puller` is its puller. The caller holds the lock.
        bool PullsNow(Guid puller) const;
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
        Guid m_healer = NoGuid;
        Trail m_trail;
        std::vector<MemberSpot> m_memberSpots;
        // Counts each change of m_memberSpots but the in-place notes.
        std::uint64_t m_memberSpotsVersion = 0;
        struct BotNote
        {
            std::int64_t atMs;
            bool joinsPulls;
        };
        std::map<Guid, BotNote> m_bots;
        std::vector<Guid> m_enemyHealers;                 // In mark order.
        BossFight m_bossFight = {NoGuid, 0};
        BossFight m_bossFought = {NoGuid, 0};
        // jvbastel/WOWAI#170: by bot, its health notes of the last
        // HealthFallMs, oldest first.
        struct HealthNote
        {
            std::int64_t atMs;
            std::uint32_t health;
        };
        std::map<Guid, std::vector<HealthNote>> m_healthNotes;
    };
}

#endif
