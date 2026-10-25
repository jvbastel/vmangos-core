#ifndef WOWAI_BOTS_TARAGAMAN_REACTION_H
#define WOWAI_BOTS_TARAGAMAN_REACTION_H

#include "BotRecipe.h"
#include "Strategy.h"

#include <cstdint>
#include <memory>

// The boss reaction for Taragaman the Hungerer in Ragefire Chasm (research
// ragefire-chasm-bosses.md). He casts Fire Nova, an instant fire area of
// about 10 yards around him, and Uppercut, a knockback of the tank.
// - The ranged bots and the healer stay more than 10 yards from him. They
//   never go into lava, nor out of his sight (WOWAI#74). With no such place,
//   they stay and take Fire Nova.
// - When a wall is near, the tank stands with its back to it, so that
//   Uppercut does not knock it into other enemies.
namespace Bots
{
    namespace Taragaman
    {
        std::uint32_t const Entry = 11520;

        // Distances in yards, from the center of the bot to his center.
        float const FireNovaYards = 12.0f; // The 10 yards of Fire Nova, and a margin.
        float const SafeYards = 16.0f;     // Where a ranged bot or the healer goes.
        float const WallNearYards = 12.0f; // A wall this near: the tank goes to it.
        float const AtWallYards = 3.0f;    // A wall this near: the back is at the wall.
        float const WallGapYards = 1.5f;   // The gap of the tank to the wall.

        std::shared_ptr<Strategy const> MakeReaction(BotRole role);
    }
}

#endif
