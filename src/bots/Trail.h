#ifndef WOWAI_BOTS_TRAIL_H
#define WOWAI_BOTS_TRAIL_H

#include "BotTypes.h"

#include <cstddef>
#include <vector>

// The tank's trail: the ground that it walked since the last fight. The
// group came that way, so no enemy waits there: it is the cleared way, on
// which the tank looks for its fight spot (FightSpot.h, jvbastel/WOWAI#105).
// The same in a play test and in a scripted run: no route is needed.
namespace Bots
{
    class BotView;
    class GroupCoordinator;

    // A new point of the trail each this many yards of the walk.
    float const TrailStepYards = 2.0f;
    // A jump of more than this between two notes is no walk (a teleport, a
    // new map): the trail starts again from there.
    float const TrailJumpYards = 20.0f;
    // The trail keeps this many of its newest points (about 200 yards).
    std::size_t const TrailMostPoints = 100;

    class Trail
    {
    public:
        // The tank stands at `at` now.
        void Note(Position const& at);
        void Clear();
        // jvbastel/WOWAI#192: the tank walks back over its trail (a retreat
        // pull of a player tank): when `at` is within TrailStepYards of an
        // older point than the last, the points after the nearest such
        // point go. The trail ends where the tank stands again.
        void CutBackTo(Position const& at);
        // Oldest first: the last point is where the tank stood last.
        std::vector<Position> const& Points() const { return m_points; }

    private:
        std::vector<Position> m_points;
    };

    // The tank keeps its trail in the group coordinator on each think: in a
    // fight, or dead, the trail is empty; after the fight it starts again
    // where the tank stands.
    void WatchTrail(BotView const& view, GroupCoordinator& group);
}

#endif
