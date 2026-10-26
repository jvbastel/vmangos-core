#ifndef WOWAI_BOTS_CHASE_WATCH_H
#define WOWAI_BOTS_CHASE_WATCH_H

#include "BotTypes.h"

#include <cstdint>
#include <vector>

// The chases beyond the limit of a scripted run (spec section 10, pass
// thresholds): no bot chases more than 25 yards from the tank. The game
// layer tells the watch, at each update of the run, which bot (not the tank)
// runs after which enemy, and how far the bot is from the living tank:
//
//   watch.Update(chases, nowMs);
//
// The bots come back to the tank at ChaseLimitYards (GroupRules.h), the
// distance of the enemy to the tank; this limit is the distance of the bot.
namespace Bots
{
    float const ChaseRecordYards = 25.0f;

    struct MemberChase
    {
        Guid member;
        Guid enemy;
        float yardsFromTank;
    };

    // One chase of a member beyond the limit, from start to end.
    struct FarChase
    {
        Guid member;
        Guid enemy;
        std::int64_t startMs; // The first update beyond the limit.
        std::int64_t endMs;   // The last update beyond the limit so far.
        float maxYards;
    };

    class ChaseWatch
    {
    public:
        // A member missing from `chases` chases nobody now.
        void Update(std::vector<MemberChase> const& chases, std::int64_t nowMs);

        std::vector<FarChase> const& Chases() const { return m_chases; }

    private:
        std::vector<FarChase> m_chases;
        std::vector<std::size_t> m_open; // The indexes of the chases that last.
    };
}

#endif
