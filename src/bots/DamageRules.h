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
}

#endif
