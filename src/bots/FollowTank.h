#ifndef WOWAI_BOTS_FOLLOW_TANK_H
#define WOWAI_BOTS_FOLLOW_TANK_H

#include "DamageRules.h"
#include "Strategy.h"

// The walk of the group behind the tank on a pull (WOWAI#92). The tank runs
// to the pack and fights it where it stands. While it does, each other bot
// that is far from it follows it at a distance, whatever its command
// (`follow`, `stay`, or none; not `come`):
// - the bot goes along its ground path to the tank, to the spot nearest to
//   the tank that keeps out of the aggro distance of each enemy, and the
//   margin, as the Priest's corpse walk does (SafePlace.h). An enemy that
//   attacks a group member is in the fight already: it does not count;
// - from that spot the bot sees the tank, so it can heal it and fight;
// - a bot that follows the tank already, such as the healer on its way to
//   a hurt tank (PriestModule), keeps that walk;
// - a bot within FollowTankYards of the tank does not move: a short pull
//   that brings the pack to the group works as before.
// The rule is under the class rules, so a bot that can fight or heal does
// that first.
namespace Bots
{
    // A bot farther than this from the tank follows it: the casting range
    // of a ranged damage bot (DamageRules.h), so a bot that can reach the
    // fight from where it stands does not move.
    float const FollowTankYards = CastingRangeYards;
    // The nearest a following bot goes to the tank, along its path.
    float const FollowTankNearYards = 12.0f;
    // The spots on the path are this far apart.
    float const FollowTankStepYards = 3.0f;

    // The rule "follow the tank to the pull" at priority `priority`.
    Rule FollowTankRule(int priority);
}

#endif
