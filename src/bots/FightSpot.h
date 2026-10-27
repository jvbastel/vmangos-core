#ifndef WOWAI_BOTS_FIGHT_SPOT_H
#define WOWAI_BOTS_FIGHT_SPOT_H

#include "BotView.h"
#include "SafePlace.h"

#include <cstdint>
#include <vector>

// The fight spot of a pull (jvbastel/WOWAI#105): where the tank holds the
// pack, so that no other pack joins. The tank steps back along its cleared
// way (Trail.h), from where it stands, to the first spot where:
// - a member at the spot pulls no other pack: it is out of the aggro
//   distance of each of its enemies plus AggroMarginYards (the safe place
//   rule, SafePlace.h);
// - the spot is more than AssistanceYards from each enemy of another pack;
// - the tank can pull the target from range: from where it stands, from a
//   spot of the trail, or from a safe place round the target (SafePlace.h).
// A pack within the assistance radius of the target's pack answers its
// call for help at the pull (Creature::CallAssistance), wherever the tank
// fights: it counts for each spot, so no spot is clean then.
// With no clean spot, the spot that brings the fewest extra packs counts,
// the newest of them first; the tank pulls anyway and warns.
namespace Bots
{
    // The core's assistance radius (CreatureFamilyAssistanceRadius). The
    // core measures it between the bodies; AssistBodyYards stands for them.
    float const AssistanceYards = 10.0f;
    float const AssistBodyYards = 1.0f;
    // Enemies this near to each other are one pack, as the packs of the
    // route generator (RouteGenerator.h, clusterDistance).
    float const PackLinkYards = 8.0f;
    // The trail spots that count: at most this far from the tank.
    float const FightSpotSearchYards = 40.0f;

    // The range of the tank's pull shot, between the bodies.
    struct ShotRange
    {
        float minYards;
        float maxYards;
    };

    struct FightSpot
    {
        Position spot = {0, 0, 0};
        // The spot passes each rule.
        bool clean = false;
        // The other packs that the pull brings with the tank at the spot:
        // 0 for a clean spot.
        std::uint32_t extraPacks = 0;
        // Where the tank pulls from, when it cannot from where it stands.
        bool hasShootSpot = false;
        Position shootSpot = {0, 0, 0};
    };

    // `enemies` in packs: enemies within PackLinkYards of each other, also
    // through other enemies, in the order of their first enemy.
    std::vector<std::vector<NearEnemy>> SplitIntoPacks(std::vector<NearEnemy> const& enemies);

    // The fight spot of the bot's pull of `target`. `trail`: the cleared
    // way, oldest first (GroupCoordinator::GetTrail). The bot's own place
    // counts first, then the trail from its newest point.
    FightSpot FindFightSpot(BotView const& view, std::vector<Position> const& trail, Guid target, ShotRange shot);
}

#endif
