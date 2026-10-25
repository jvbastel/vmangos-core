#ifndef WOWAI_BOTS_THREAT_WATCH_H
#define WOWAI_BOTS_THREAT_WATCH_H

#include "BotTypes.h"

#include <cstdint>
#include <vector>

// The threat losses of a scripted run. A threat loss is an enemy that hits a
// group member who is not the tank for more than 4 seconds without a break
// (spec section 10, pass thresholds). The game layer tells the watch, at
// each update of the run, which enemy hits which member:
//
//   watch.Update(hitsOnNonTanks, nowMs);
namespace Bots
{
    std::int64_t const ThreatLossMs = 4000;

    struct EnemyOnMember
    {
        Guid enemy;
        Guid member; // For the threat watch, not the tank.
    };

    struct ThreatLoss
    {
        Guid enemy;
        Guid member;
        std::int64_t startMs; // The first update with the hit.
        std::int64_t endMs;   // The last update with the hit so far.
    };

    class ThreatWatch
    {
    public:
        // A pair missing from `hits` ends its hit.
        void Update(std::vector<EnemyOnMember> const& hits, std::int64_t nowMs);

        // In the order they passed 4 seconds.
        std::vector<ThreatLoss> const& Losses() const { return m_losses; }

    private:
        struct Hit
        {
            EnemyOnMember pair;
            std::int64_t startMs;
            int loss; // The index in m_losses, or -1.
        };

        std::vector<Hit> m_hits;
        std::vector<ThreatLoss> m_losses;
    };

    // The lost mobs of a scripted run (WOWAI#73 review). A lost mob is an
    // enemy that hits a group member, the tank too, while no bot attacks it,
    // for more than the limit without a break. A threat loss is part of a
    // fight; a lost mob is a fight that nobody fights, so it fails the run.
    // The game layer tells the watch, at each update of the run, which
    // enemy hits which member and which enemies a bot or a pet attacks:
    //
    //   watch.Update(hitsOnMembers, attackedEnemies, nowMs);
    std::int64_t const LostMobMs = 10000;

    struct LostMob
    {
        Guid enemy;
        Guid member;          // The member it hit at the last update.
        std::int64_t startMs; // The first update of the unattacked hits.
        std::int64_t endMs;   // The last update of them so far.
    };

    class LostMobWatch
    {
    public:
        explicit LostMobWatch(std::int64_t limitMs = LostMobMs) : m_limitMs(limitMs) {}

        // An enemy missing from `hits`, or in `attacked`, starts its time
        // again.
        void Update(std::vector<EnemyOnMember> const& hits, std::vector<Guid> const& attacked, std::int64_t nowMs);

        // In the order they passed the limit. An enemy is lost once.
        std::vector<LostMob> const& LostMobs() const { return m_lost; }
        std::int64_t LimitMs() const { return m_limitMs; }

    private:
        struct Unattacked
        {
            Guid enemy;
            std::int64_t startMs;
        };

        std::int64_t m_limitMs;
        std::vector<Unattacked> m_unattacked;
        std::vector<LostMob> m_lost;
    };
}

#endif
