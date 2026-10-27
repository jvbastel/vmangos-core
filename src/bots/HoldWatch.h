#ifndef WOWAI_BOTS_HOLD_WATCH_H
#define WOWAI_BOTS_HOLD_WATCH_H

#include "GroupCoordinator.h"

#include <cstdint>
#include <string>
#include <vector>

// The held pull in the trace of a scripted run (jvbastel/WOWAI#127): when
// the tank engages its pull target, when the target reaches the tank (melee
// contact, NoteFocusContact), when the tank is back at its fight spot, and
// when the tank holds threat (GroupCoordinator::TankHoldsThreat) and why:
// "melee contact" or "time limit" (HoldLimitMs).
namespace Bots
{
    class HoldWatch
    {
    public:
        // The pull of `group` at `nowMs` (the clock of the bots). The trace
        // lines of what changed since the last call.
        std::vector<std::string> Observe(GroupCoordinator const& group, std::int64_t nowMs);

    private:
        bool m_holding = false;
        bool m_contact = false;
        bool m_back = false;
        bool m_held = false;
    };
}

#endif
