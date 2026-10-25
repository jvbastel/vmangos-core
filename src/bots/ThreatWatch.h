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
        Guid member; // Not the tank.
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
}

#endif
