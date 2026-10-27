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
    // 3. a loose enemy: an enemy in the chase limit that hits a group
    //    member other than the living tank, the bot itself too. The bot
    //    joins a fight that it did not see start, also when it stays or
    //    follows. It takes the enemy that only it attacks, else one that no
    //    member attacks, so that no enemy stays lost. Not the focus that the
    //    bot waits a taunt for;
    // 4. the focus target, once the tank holds threat on it
    //    (TankHoldsThreat below), while it is in
    //    the chase limit (GroupRules.h). Outside a pull, not while it hits
    //    another member and the tank lives: the bot waits for the taunt,
    //    unless the tank gave it up (GroupCoordinator::GiveUpTaunt).
    Guid PickDamageTarget(BotContext const& context);

    // The damage target of a melee bot (PickDamageTarget): not one in the
    // reach of a pack outside the fight (KeepOut.h, jvbastel/WOWAI#121).
    // The keep-out rule would step the bot out again, and its chase take it
    // back in: a loop. The member that the enemy hits steps out itself and
    // brings it along, as for the tank's pick-up.
    Guid PickMeleeDamageTarget(BotContext const& context);

    // True when the bot has a damage target now.
    bool HasDamageTarget(BotContext const& context);

    // jvbastel/WOWAI#127: the tank holds threat on the focus now
    // (GroupCoordinator::TankHoldsThreat). In a held pull, the bot first
    // notes the melee contact when it sees the pull target attack the
    // puller within MeleeContactYards (CommonRules.h, between the edges,
    // BotView::GetDistanceBetween): a ranged-pulled target that runs to
    // the tank is not the tank's yet.
    bool TankHoldsThreat(BotContext const& context);

    // The casting range of a ranged damage bot, and its distance from the
    // damage target when it runs closer. The casting range counts by
    // BotView::GetSpellDistance, as the range of a cast does: else, at the
    // edge, Frostbolt (30 yards) fails and Fireball (35) goes instead. The
    // stand distance is the chase distance of the movement, well inside it.
    float const CastingRangeYards = 30.0f;
    float const RangedStandYards = 25.0f;

    // A rule of a ranged damage bot: it runs into casting range of the
    // damage target.
    Rule CloseInRule(int priority);
}

#endif
