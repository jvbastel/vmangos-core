#ifndef WOWAI_BOTS_PEEL_WATCH_H
#define WOWAI_BOTS_PEEL_WATCH_H

#include "BotTypes.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// The result facts of the peel nova and the step back of a scripted run
// (jvbastel/WOWAI#145, spec #142). The game layer tells the watch, at each
// update of the run, what each enemy of the fight does and where the healer
// stands:
//
//   watch.Update(seen, nowMs);
//
// The watch finds each Frost Nova (the freezes that start in one update by
// one caster), its victims, and each freeze: when it ended, when the tank
// arrived, and whether party damage broke it before the tank arrived. For a
// freeze of an enemy on the healer it keeps where the healer stands when
// the freeze ends. PeelFailure judges the `expect peel_nova` of a scenario
// (RunScript.h).
namespace Bots
{
    // The nova comes this soon after the enemy reached the healer.
    std::int64_t const PeelNovaLimitMs = 2000;
    // A freeze this long ran out: Frost Nova holds up to 8 s.
    std::int64_t const FreezeRanOutMs = 7500;
    // A freeze that starts this soon after a Frost Nova went out is of that
    // nova.
    std::int64_t const NovaFreezeLinkMs = 1000;

    struct PeelEnemySeen
    {
        Guid enemy = NoGuid;
        Guid victim = NoGuid;     // The member that it attacks, or NoGuid.
        float victimYards = 0.0f; // To the victim, between the edges.
        Guid frozenBy = NoGuid;   // The caster of its Frost Nova, NoGuid when not frozen.
        // The tank arrived: the enemy attacks the tank, or the tank attacks
        // it in melee.
        bool tankHasIt = false;
        std::uint32_t health = 0;
        float healerYards = 0.0f; // To the living healer, between the centers.
    };

    // A Frost Nova that went out in this update (final review of spec
    // #142): it can freeze nothing, for example on a resist.
    struct PeelNovaOut
    {
        Guid caster = NoGuid;
        // The enemies of the fight within its radius (10 yd, 3D, to the edge).
        std::vector<Guid> enemiesInRadius;
    };

    // An enemy missing from `enemies` died or went away.
    struct PeelSeen
    {
        Guid tank = NoGuid;
        Guid healer = NoGuid;
        float healerTankYards = 0.0f; // Between the centers.
        bool healerSeesTank = false;
        std::vector<PeelEnemySeen> enemies;
        std::vector<PeelNovaOut> novasOut;
    };

    // A Frost Nova that went out (PeelNovaOut).
    struct PeelCast
    {
        Guid caster = NoGuid;
        std::int64_t ms = 0;
        std::size_t enemiesInRadius = 0;
        // The time from the enemy reaching the healer to the nova, when an
        // enemy in melee of the healer was in its radius, or -1.
        std::int64_t healerReachMs = -1;
        // A freeze of the caster started within NovaFreezeLinkMs.
        bool froze = false;
    };

    struct PeelNova
    {
        Guid caster = NoGuid;
        std::int64_t ms = 0;
        // The members other than the tank that the frozen enemies attacked
        // (PeelFreeze::victim).
        std::vector<Guid> victims;
        // A victim is not the caster.
        bool forOthers = false;
        // The time from the enemy reaching the healer (MeleeHitYards,
        // CommonRules.h) to the nova, or -1 when no frozen enemy was on the
        // healer.
        std::int64_t healerReachMs = -1;
    };

    struct PeelFreeze
    {
        Guid enemy = NoGuid;
        std::size_t nova = 0; // The index in PeelWatch::Novas.
        // The member that the enemy hit in melee in the update before the
        // freeze, else the one it attacks at the freeze.
        Guid victim = NoGuid;
        std::int64_t startMs = 0;
        std::int64_t endMs = 0; // The last update so far while it lasts.
        bool open = true;
        std::int64_t tankMs = -1; // When the tank arrived, or -1.
        // When the enemy first attacked the tank while still frozen: the
        // tank picked it up (final review of spec #142, finding 14), or -1.
        std::int64_t tankHoldsMs = -1;
        // Party damage hit the frozen enemy before the tank arrived, and the
        // freeze ended before it ran out.
        bool brokenByParty = false;
        // The victim was the healer: where the healer stands at the end.
        bool onHealer = false;
        float healerYards = 0.0f;
        float healerTankYards = 0.0f;
        bool healerSeesTank = false;
    };

    class PeelWatch
    {
    public:
        void Update(PeelSeen const& seen, std::int64_t nowMs);

        std::vector<PeelNova> const& Novas() const { return m_novas; }
        std::vector<PeelFreeze> const& Freezes() const { return m_freezes; }
        std::size_t NovasForOthers() const;
        // The novas with a freeze that party damage broke before the tank
        // arrived.
        std::size_t NovasBrokenByParty() const;
        bool NovaBrokenByParty(std::size_t nova) const;
        std::vector<PeelCast> const& Casts() const { return m_casts; }
        // The casts with an enemy of the fight in the radius and no freeze.
        std::vector<PeelCast> NovasThatFrozeNothing() const;
        // The longest time that one enemy attacked the healer without a break.
        std::int64_t LongestOnHealerMs() const { return m_longestOnHealerMs; }

    private:
        struct Track
        {
            Guid victim = NoGuid;
            std::int64_t reachMs = -1; // In melee of the victim since, or -1.
            std::uint32_t health = 0;
            bool open = false;         // A freeze lasts.
            std::size_t freeze = 0;    // Its index in m_freezes.
            bool hitBeforeTank = false;
            std::int64_t onHealerMs = -1; // Attacks the healer since, or -1.
        };

        void EndFreeze(Track& track, bool alive, std::int64_t nowMs);

        std::vector<PeelNova> m_novas;
        std::vector<PeelFreeze> m_freezes;
        std::vector<PeelCast> m_casts;
        std::map<Guid, Track> m_tracks;
        std::int64_t m_longestOnHealerMs = 0;
    };

    // Why the peel of a scenario failed, or "" when it passed: the first
    // nova on the healer's enemy came within PeelNovaLimitMs of the enemy
    // reaching the healer, party damage broke no freeze before the tank
    // arrived, and at the end of each freeze on the healer the healer stood
    // at least StepBackYards from the enemy, within StepBackHealYards of the
    // tank and in its sight (StepBack.h).
    // Final review of spec #142, finding 14: a freeze that the tank picked up
    // while it lasted (tankHoldsMs) needs no step back. A first nova that
    // went out with the healer's enemy in its radius and froze nothing (a
    // resist) does not fail alone: it fails when an enemy then stayed on the
    // healer past ThreatLossMs (ThreatWatch.h). A lost mob fails the run by
    // itself (LostMobWatch).
    std::string PeelFailure(PeelWatch const& watch);
}

#endif
