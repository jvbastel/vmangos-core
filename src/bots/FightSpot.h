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
//   the target (SafePlace.h).
// A pack within the assistance radius of the target's pack answers its
// call for help at the pull (Creature::CallAssistance), wherever the tank
// fights: it counts for each spot, so no spot is clean then.
// With no clean spot, the spot that brings the fewest extra packs counts,
// the newest of them first; the tank pulls anyway and warns.
// A caster pack (jvbastel/WOWAI#121) does not come to the tank: its caster
// stays at its spawn and casts. For it the tank first looks for a
// line-of-sight pull: a spot that passes the rules above and that the
// caster cannot see from its spawn, with a shot at the target from within
// LineOfSightShotYards of it. The caster then has to come round to see the
// tank.
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
    // The shot of a line-of-sight pull goes from at most this far from its
    // fight spot, so the tank is back there before the caster sees it.
    float const LineOfSightShotYards = 10.0f;
    // A focus that stays away this long, on ground where a fight pulls
    // other packs, makes the tank run further out along its cleared way
    // (FindRunOutSpot), so the focus follows it there.
    std::int64_t const FocusComeMs = 3 * 1000;
    // Each run further out goes at least this far along the cleared way.
    float const RunOutStepYards = 8.0f;

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
        // A line-of-sight pull of a caster pack: the target cannot see the
        // spot from where it stands.
        bool lineOfSight = false;
        // The target's pack, and the other packs round the pull, for the
        // warn when one of them joins the fight (WarriorModule.h).
        std::vector<Guid> targetPack;
        std::vector<std::vector<Guid>> otherPacks;
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

    // A caster pack: an enemy of `pack` has mana (jvbastel/WOWAI#121).
    bool IsCasterPack(BotView const& view, std::vector<NearEnemy> const& pack);

    // The living enemies out of the fight round `at` (none in combat), in
    // packs: the packs that a member at a place near `at` could pull.
    std::vector<std::vector<NearEnemy>> PacksOutsideTheFight(BotView const& view, Position const& at);

    // The packs out of the fight that a member at `at` pulls: it stands
    // within the aggro distance and AggroMarginYards of one of their
    // enemies (PullsAnEnemy). 0: a member may fight at `at`.
    std::uint32_t PacksPulledAt(BotView const& view, Position const& at);

    // Where the tank leads a focus at `focusAt` that does not come to it at
    // `from`: the newest spot of the cleared way `way` (oldest first) that is
    // RunOutStepYards or more from `from`, farther from the focus than
    // `from`, within FightSpotSearchYards, and pulls no pack out of the
    // fight. A spot that the focus cannot see counts first. False when no
    // spot of the way is such a spot.
    bool FindRunOutSpot(BotView const& view, std::vector<Position> const& way, Position const& from,
        Position const& focusAt, Position& spot);

    // The fight spot of the bot's pull of `target`. `trail`: the cleared
    // way, oldest first (GroupCoordinator::GetTrail). The bot's own place
    // counts first, then the trail from its newest point.
    FightSpot FindFightSpot(BotView const& view, std::vector<Position> const& trail, Guid target);
}

#endif
