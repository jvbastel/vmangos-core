#ifndef WOWAI_BOTS_WIPE_REVIVE_H
#define WOWAI_BOTS_WIPE_REVIVE_H

#include <cstdint>
#include <vector>

// The wipe revive of a scripted run (jvbastel/WOWAI#72, #85): when every
// member is dead, the run revives them all at the dungeon entrance after a
// delay. The rule for a member that is not plainly alive or dead:
// - A member not in the world (logged out, or not found) is left out. It
//   does not hold the others dead, and it does not revive with them.
// - A member in a teleport can land alive, so it holds the wipe. The delay
//   starts again when it lands dead.
// - With no member in the world, nobody revives.
namespace Bots
{
    // The time from the wipe to the revive.
    std::int64_t const WipeReviveDelayMs = 5000;
    // A delay of WipeWatch::Update: the members never revive.
    std::int64_t const NeverRevive = -1;

    enum class RunMember
    {
        Living,     // In the world and alive.
        Dead,       // In the world and dead, as a corpse or a ghost.
        InTeleport, // In the world, in a teleport.
        NotInWorld, // Not in the world (logged out, or not found).
    };

    class WipeWatch
    {
    public:
        // The members of the run now; `nowMs`: any steady clock in
        // milliseconds; `delayMs`: the time from the wipe to the revive,
        // NeverRevive for no revive (the `release never` line of a script).
        // True once per wipe, when the members in the world revive now.
        bool Update(std::vector<RunMember> const& members, std::int64_t nowMs,
            std::int64_t delayMs = WipeReviveDelayMs);
        // True when the last Update began a wipe.
        bool Began() const { return m_began; }

    private:
        std::int64_t m_wipeMs = -1; // When the wipe began; -1: no wipe.
        bool m_began = false;
    };
}

#endif
