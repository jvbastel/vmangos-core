#ifndef WOWAI_BOTS_DAMAGE_RULES_H
#define WOWAI_BOTS_DAMAGE_RULES_H

#include "Strategy.h"

// The targeting rules of all damage bots (spec 6, Pulls and targets): the
// Rogue, the Warlock, and the Mage. The damage bots attack the focus target,
// the target of the tank. On a pull, they wait until the tank holds threat.
// `attack` changes their target to the target of the player.
namespace Bots
{
    // The damage target of the bot, or NoGuid:
    // 1. the attack target of the player (`attack`), at once;
    // 2. an enemy healer in the fight (GroupRules.h), until it dies;
    // 3. the focus target, once the tank holds threat on it, while it is in
    //    the chase limit (GroupRules.h);
    // 4. an enemy that attacks the bot itself, in the chase limit.
    Guid PickDamageTarget(BotContext const& context);

    // True when the bot has a damage target now.
    bool HasDamageTarget(BotContext const& context);

    // The casting range of a ranged damage bot, and its distance from the
    // damage target when it runs closer.
    float const CastingRangeYards = 30.0f;
    float const RangedStandYards = 25.0f;

    // A rule of a ranged damage bot: it runs into casting range of the
    // damage target.
    Rule CloseInRule(int priority);
}

#endif
