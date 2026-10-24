#ifndef WOWAI_BOTS_DAMAGE_RULES_H
#define WOWAI_BOTS_DAMAGE_RULES_H

#include "Strategy.h"

// The targeting rules of all damage bots (spec 6, Pulls and targets). The
// damage bots attack the focus target, the target of the tank. On a pull,
// they wait until the tank holds threat.
namespace Bots
{
    // True when the damage bot may attack the focus target now: the focus
    // target lives, and no pull waits for the tank to take threat. Without a
    // pull, the focus target is in the fight already (the tank fights back,
    // or the player names it with `attack`).
    bool MayAttackFocus(BotContext const& context);

    // The casting range of a ranged damage bot, and its distance from the
    // focus target when it runs closer.
    float const CastingRangeYards = 30.0f;
    float const RangedStandYards = 25.0f;

    // A rule of a ranged damage bot: it runs into casting range of the focus
    // target when it may attack it.
    Rule CloseInRule(int priority);
}

#endif
