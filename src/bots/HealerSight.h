#ifndef WOWAI_BOTS_HEALER_SIGHT_H
#define WOWAI_BOTS_HEALER_SIGHT_H

#include "BotView.h"
#include "GroupCoordinator.h"
#include "Strategy.h"

// jvbastel/WOWAI#195 (spec #188, story 37): the healer's sight of a player
// tank. In a fight of a player-tank group (GroupCoordinator::
// IsPlayerTankGroup), a healer that does not see the living tank, or stands
// beyond the heal reach of the Priest (PriestModule.h), walks to a spot that
// sees the tank and is in that reach, for example after the tank stepped
// behind a corner for a line-of-sight pull. The spots are those of the
// cast place round the tank (SafePlace.h, FindSafeCastPlace), the nearest
// first, each safe of the packs outside the fight (KeepOut.h).
// Not while the healer casts: a step would stop the heal. A bot-tank group
// keeps its healer as it was.
namespace Bots
{
    // The healer's spots keep the tank this near: well inside the heal
    // reach, so that a step of the tank does not take it out again.
    float const TankSightMostYards = 30.0f;

    // The spot for the healer, or false when it sees the tank in reach, or
    // finds no safe spot.
    bool FindTankSightSpot(BotContext const& context, Position& spot);

    // The rule "keep the tank in sight" of the healer, at `priority`.
    Rule TankSightRule(int priority);
    char const* const TankSightRuleName = "keep the tank in sight";

    // Story 40: after the fight of a player-tank group, true for a living
    // bot that knows the resurrect of the Priest and has the mana for its
    // rank on the dead tank (not released, its resurrect not given up).
    // Such a bot resurrects the tank before it rests: it does not start to
    // eat or drink (DeathAndRest.h), and a bot that already sits stands up
    // (BotAI), since a sitting bot casts nothing. False in a bot-tank group.
    bool ResurrectsTankBeforeRest(BotView const& view, GroupCoordinator const& group);
}

#endif
