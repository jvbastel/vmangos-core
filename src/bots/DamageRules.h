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
    // 3. the focus target, once the tank holds threat on it
    //    (TankHoldsThreat below), while it is in
    //    the chase limit (GroupRules.h). Outside a pull, not while it hits
    //    another member and the tank lives: the bot waits for the taunt,
    //    unless the tank gave it up (GroupCoordinator::GiveUpTaunt).
    // While the tank lives, the bot leaves a loose enemy (one that hits a
    // member other than the tank) to the tank: peeling it makes more loose
    // enemies (jvbastel/WOWAI#171). With no living tank, nobody picks one
    // up: a loose enemy in the chase limit comes before the focus. The bot
    // takes the enemy that only it attacks, else one that no member
    // attacks, so that no enemy stays lost. Not the focus that the bot
    // waits a taunt for.
    Guid PickDamageTarget(BotContext const& context);

    // A damage bot this near the tank (BotView::GetDistance) is at the
    // tank: an enemy on it is in the tank's reach (jvbastel/WOWAI#171). More
    // than the farthest follow distance of the adapter (6 yards), so that a
    // bot that follows the tank there stays at it.
    float const AtTankYards = 8.0f;

    // The damage target of a melee bot (PickDamageTarget), else, at the
    // tank (AtTankYards), an enemy in the chase limit that attacks the bot
    // (jvbastel/WOWAI#171): the bot is not idle while the enemy is where
    // the tank reaches it. Not one in the
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

    // A rule of each damage bot (jvbastel/WOWAI#171): when a living enemy
    // attacks the bot and it is farther than AtTankYards from the living
    // tank, it walks to the tank, so that the tank can take the enemy off
    // it. It keeps hitting the focus once there. Not with an attack target
    // of the player.
    Rule WalkToTankRule(int priority);
}

#endif
