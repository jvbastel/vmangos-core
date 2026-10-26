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

    enum class RunMember
    {
        Living,     // In the world and alive.
        Dead,       // In the world and dead, as a corpse or a ghost.
        InTeleport, // In the world, in a teleport.
        Away,       // Not in the world.
    };

    class WipeWatch
    {
    public:
        // The members of the run now; `nowMs`: any steady clock in
        // milliseconds. True once per wipe, when the members in the world
        // revive now.
        bool Update(std::vector<RunMember> const& members, std::int64_t nowMs);

    private:
        std::int64_t m_wipeMs = -1; // When the wipe began; -1: no wipe.
    };
}

#endif
