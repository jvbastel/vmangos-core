#ifndef WOWAI_BOTS_WISP_TRAIL_H
#define WOWAI_BOTS_WISP_TRAIL_H

#include "BotTypes.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <vector>

// The wisp trail of a route walk (WOWAI#90): the waypoint wisps that
// `.route next` and `.route back` put on the points, oldest first. A
// character may have only so many summons, so a full trail does not summon
// a new wisp: it moves its oldest wisp to the new place.
namespace Bots
{
    // The most wisps in one trail.
    std::size_t const WispTrailSize = 100;

    class WispTrail
    {
    public:
        enum class Action
        {
            Summon, // Summon a new wisp, then call Summoned with it.
            Move,   // Move the wisp `guid` to the place. It is now the newest.
            None    // No wisp to summon or to move.
        };

        struct Choice
        {
            Action action = Action::None;
            Guid guid = NoGuid;
        };

        // Tells if a wisp is still on the map.
        using IsOnMap = std::function<bool(Guid)>;

        // What to do for the next place. First the wisps that are no longer
        // on the map drop out of the trail. Then a trail with fewer than
        // WispTrailSize wisps summons, while the character has a free
        // summon slot (`freeSummons`). Else the oldest wisp moves.
        Choice Next(IsOnMap const& isOnMap, std::size_t freeSummons);
        // A new wisp at the end of the trail.
        void Summoned(Guid guid);
        // Empties the trail, and gives back its wisps to remove.
        std::vector<Guid> Clear();
        // The wisps, oldest first.
        std::vector<Guid> Wisps() const;

    private:
        std::deque<Guid> m_wisps;
    };
}

#endif
