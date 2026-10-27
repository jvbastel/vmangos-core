#ifndef WOWAI_BOTS_RUN_TRACE_H
#define WOWAI_BOTS_RUN_TRACE_H

#include "BotTypes.h"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

// The trace of a scripted run (spec section 10, Results): on a fail or an
// abort, the run saves the last 3 minutes, one line per event. The events are
// the commands of the test driver and the positions, targets, and casts of
// each group member. Each line starts with the run time in seconds:
//
//   61.4 line 12: fight pack1
//   61.4 Ezzra says in party: pull
//   61.5 Grukk at -4.94 -22.40 -20.20
//   62.0 Grukk targets Earthborer 38043
//   62.1 Grukk casts 2457 Battle Stance
namespace Bots
{
    std::int64_t const TraceKeepMs = 3 * 60000;

    // What the trace sees of one member at one update.
    struct MemberState
    {
        std::string name;
        Position position;
        std::string target;    // Empty: no target.
        std::uint32_t spell;   // The spell that it casts now, or 0.
        std::string spellName;
    };

    class RunTrace
    {
    public:
        explicit RunTrace(std::int64_t keepMs = TraceKeepMs);

        void Add(std::int64_t ms, std::string const& line);
        // A position after a move of 1 yard or more, at most once a second;
        // a new target; a new cast.
        void Observe(std::int64_t ms, MemberState const& member);

        // The events of the last 3 minutes before the newest one.
        std::vector<std::string> Lines() const;
        std::string Text() const;

    private:
        struct Seen
        {
            std::int64_t positionMs;
            Position position;
            std::string target;
            std::uint32_t spell;
        };

        std::int64_t m_keepMs;
        std::deque<std::pair<std::int64_t, std::string>> m_events;
        std::map<std::string, Seen> m_seen;
    };
}

#endif
