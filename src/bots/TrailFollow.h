#ifndef WOWAI_BOTS_TRAIL_FOLLOW_H
#define WOWAI_BOTS_TRAIL_FOLLOW_H

#include "Strategy.h"

#include <vector>

// jvbastel/WOWAI#192: out of combat, the bots of a player-tank group follow
// like players. The group coordinator keeps the tank's trail (Trail.h): each
// bot notes where the human tank stands on each think (WatchTankTrail). In a
// player-tank group a trail that the tank walks back over is cut back to
// where the tank stands again (Trail::CutBackTo), so the trail is always the
// cleared way from behind the group to the tank.
// - Each bot has a spot behind the tank along the trail, by role: melee
//   damage 3-6 yd, healer 10-15 yd, ranged damage 12-20 yd. Two bots of one
//   role get different distances in the band, and each bot gets its own
//   sideways offset from the trail, so that they do not stack.
// - The bot walks along the trail to its spot, not in a straight line: it
//   goes to the farthest point of the trail that it can reach with no
//   corner cut.
// - A retreat pull (the tank walks back toward the bots): the trail is cut
//   back, the spots move back with it, and the bots walk back too.
// - A bot in combat does not follow the trail: today's fight rules take
//   over. A bot not in combat yet follows it also while the tank or the
//   group fights, along the cleared way of the fight, cut back to the tank
//   (spec #188 fix-up S3, a retreat pull).
// Bot-tank groups do not change: the rule is for a player-tank group only.
namespace Bots
{
    class BotView;
    class GroupCoordinator;

    // The distance band behind the tank, along its trail.
    struct TrailBand
    {
        float nearYards;
        float farYards;
    };

    TrailBand const MeleeTrailBand = {3.0f, 6.0f};
    TrailBand const HealerTrailBand = {10.0f, 15.0f};
    TrailBand const RangedTrailBand = {12.0f, 20.0f};
    // The sideways offset from the trail of the first bot; the next bots
    // take each side in turn, one more step out each pair.
    float const TrailSideStepYards = 1.5f;
    // A bot this near its spot is in place: it does not move.
    float const TrailInPlaceYards = 1.5f;
    // The walk to the spot cuts no corner of the trail by more than this.
    float const TrailCornerCutYards = 1.5f;
    // A bot farther than this from the trail walks straight to its spot
    // (the game finds the ground path).
    float const TrailJoinYards = 30.0f;

    // The band of role `role`. The tank has none: the melee band.
    TrailBand TrailBandOf(BotRole role);

    // The follow spot of a bot: `way` runs from the tank back along its
    // trail (the tank's place first, then the trail newest first).
    // `inRole`/`roleCount`: the bot is the inRole-th (from 0) of roleCount
    // bots of its role; `sideIndex`: the bot is the sideIndex-th bot of the
    // group. A way shorter than the distance goes on in the direction of its
    // last leg, or toward `self` for a way of one point. The sideways
    // offset ends short of a wall (BotView::GetWallDistance), on the ground.
    Position FindTrailSpot(BotView const& view, std::vector<Position> const& way, Position const& self, BotRole role,
        std::size_t inRole, std::size_t roleCount, std::size_t sideIndex);

    // The way from the tank back along its trail, tank first (see
    // FindTrailSpot).
    std::vector<Position> TankWay(Position const& tank, std::vector<Position> const& trail);

    // The next place to walk to from `self` to `spot`, along `way`: the
    // farthest point of the way between the bot and the spot's place on the
    // way that the bot reaches in a straight line that cuts no corner by
    // more than TrailCornerCutYards; the spot itself when the way is
    // straight to it.
    Position NextTrailStep(std::vector<Position> const& way, Position const& self, Position const& spot);

    // Each bot of a player-tank group calls it on each think: the trail of
    // the human tank (GroupCoordinator::KeepTrail). It does nothing in a
    // bot-tank group: the bot tank notes its own trail (WatchTrail).
    void WatchTankTrail(BotView const& view, GroupCoordinator& group);

    // True for a bot of a player-tank group whose command player is the
    // tank: the trail follow replaces "follow the player".
    bool FollowsTheTankTrail(BotContext const& context);

    char const* const FollowTankTrailRule = "follow the tank's trail";
    // The rule "follow the tank's trail" at priority `priority`.
    Rule TrailFollowRule(int priority);
}

#endif
