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
// - the tank can pull the target from range (InShotFrom, Ranged.h): from
//   where it stands, from a spot of the trail, or from a safe place round
//   the target (SafePlace.h). Else from the first point of its ground path
//   to the target with a shot (FindShotOnPath, Ranged.h, for a target on a
//   ledge): no checked way, so the spot is not clean.
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
    // Nearer than this to a spot of the pull (the fight spot, the shoot
    // spot, a member spot, MemberSpot.h), a bot is there.
    float const AtSpotYards = 2.0f;
    // The trail spots that count: at most this far from the tank.
    float const FightSpotSearchYards = 40.0f;

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

    // `enemies` split for the pull of `target` at `targetAt`: the target's
    // pack (the target alone when no enemy of `enemies` is the target), and
    // the other packs.
    struct TargetPack
    {
        std::vector<NearEnemy> pack;
        std::vector<std::vector<NearEnemy>> others;
    };
    TargetPack SplitTargetPack(std::vector<NearEnemy> const& enemies, Guid target, Position const& targetAt);

    // The fight spot of the bot's pull of `target`. `trail`: the cleared
    // way, oldest first (GroupCoordinator::GetTrail). The bot's own place
    // counts first, then the trail from its newest point.
    FightSpot FindFightSpot(BotView const& view, std::vector<Position> const& trail, Guid target);
}

#endif
