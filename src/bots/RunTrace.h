#ifndef WOWAI_BOTS_RUN_TRACE_H
#define WOWAI_BOTS_RUN_TRACE_H

#include "BotTypes.h"

#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

// The trace of a scripted run (spec section 10, Results): on a fail or an
// abort, the run saves the last 3 minutes, one line per event. A full run
// keeps and saves the whole run, also when it passes (jvbastel/WOWAI#152),
// so that the dungeon map can draw where the group walked. The events are
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
    // Keep every event of the run: a window longer than any run.
    std::int64_t const TraceKeepWholeRun = std::numeric_limits<std::int64_t>::max();

    // What the trace sees of one member at one update.
    struct MemberState
    {
        std::string name;
        Position position;
        std::string target;    // Empty: no target.
        std::uint32_t spell;   // The spell that it casts now, or 0.
        std::string spellName;
    };

    // The trace line of an enemy of the fight that starts to flee
    // (jvbastel/WOWAI#121, for #109): "<enemy> flees at <x> <y> <z>", where
    // it was then.
    std::string FleeLine(std::string const& enemy, Position const& at);

    class RunTrace
    {
    public:
        // keepMs: how long before the newest event the trace keeps events,
        // or TraceKeepWholeRun.
        explicit RunTrace(std::int64_t keepMs = TraceKeepMs);

        void Add(std::int64_t ms, std::string const& line);
        // A position after a move of 1 yard or more, at most once a second;
        // a new target; a new cast.
        void Observe(std::int64_t ms, MemberState const& member);

        // The kept events: those of the last keepMs before the newest one.
        std::vector<std::string> Lines() const;
        std::string Text() const;
        // The lines added since the last call, for the live trace file of
        // the run (jvbastel/WOWAI#98), also those that the trace no longer
        // keeps. The game layer takes them at each update.
        std::vector<std::string> TakeNew();

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
        std::vector<std::pair<std::int64_t, std::string>> m_new; // Since the last TakeNew.
        std::map<std::string, Seen> m_seen;
    };
}

#endif
