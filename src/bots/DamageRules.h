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
    // 2. an enemy healer in the fight (GroupRules.h), until it dies, while
    //    the bot does not hold back on its threat (HoldsBackOnThreat);
    // 3. the focus target, once the tank holds threat on it
    //    (TankHoldsThreat below), while the bot does not hold back on its
    //    threat (HoldsBackOnThreat, jvbastel/WOWAI#194), while it is in
    //    the chase limit (GroupRules.h). Not while it hits another member
    //    and the tank lives: the bot waits for the taunt, unless the tank
    //    gave it up (GroupCoordinator::GiveUpTaunt), or it is the target of
    //    the pull.
    // While the tank lives, the bot leaves a loose enemy (one that hits a
    // member other than the tank) to the tank: peeling it makes more loose
    // enemies (jvbastel/WOWAI#171). With no living tank, nobody picks one
    // up: a loose enemy in the chase limit comes before the focus. The bot
    // takes the enemy that only it attacks, else one that no member
    // attacks, so that no enemy stays lost. Not the focus that the bot
    // waits a taunt for.
    // jvbastel/WOWAI#195: in a player-tank group whose tank the bot sees
    // dead, the living focus in the chase limit comes before the loose
    // enemies, with no threat wait and no threat throttle
    // (HoldsBackOnThreat): the bots fight on against it.
    // jvbastel/WOWAI#144: a damage bot, ranged or melee, holds fire on an
    // enemy frozen by Frost Nova (HoldsFireOn) in all steps but the first:
    // a hit breaks the freeze. DoTs already on it stay.
    Guid PickDamageTarget(BotContext const& context);

    // jvbastel/WOWAI#144: Frost Nova holds enemy `enemy` frozen now. The one
    // test of a frozen enemy for the hold of fire, the step back, and the
    // peel nova (MageModule.cpp, jvbastel/WOWAI#143).
    bool IsFrozen(BotView const& view, Guid enemy);

    // jvbastel/WOWAI#144: enemy `enemy` is frozen by Frost Nova, and its
    // target is not the tank: a damage bot (ranged or melee, final review of
    // spec #142, finding 5) starts no new attack on it, so that the freeze
    // holds until the tank picks it up or it ends (8 s). Once the tank is its
    // target, the tank's own hits break the freeze anyway.
    bool HoldsFireOn(BotContext const& context, Guid enemy);

    // A damage bot this near the tank (BotView::GetDistance) is at the
    // tank: an enemy on it is in the tank's reach (jvbastel/WOWAI#171). More
    // than the farthest follow distance of the adapter (6 yards), so that a
    // bot that follows the tank there stays at it.
    float const AtTankYards = 8.0f;

    // The damage target of a melee bot (PickDamageTarget), else, at the
    // tank (AtTankYards), an enemy in the chase limit that attacks the bot
    // (jvbastel/WOWAI#171): the bot is not idle while the enemy is where
    // the tank reaches it. Both with the same hold of fire on frozen
    // enemies (HoldsFireOn; jvbastel/WOWAI#142, final review finding 5).
    // The attack target of the player is never held (the player's command
    // wins). The target is also not one in the
    // reach of a pack outside the fight (KeepOut.h, jvbastel/WOWAI#121).
    // The keep-out rule would step the bot out again, and its chase take it
    // back in: a loop. The member that the enemy hits steps out itself and
    // brings it along, as for the loose enemies that the tank takes.
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

    // jvbastel/WOWAI#160: a rule of a ranged damage bot. With its damage
    // target in casting range but out of its sight (BotView::CanSeeFrom, as
    // a cast checks it), the bot casts nothing, and CloseInRule does not
    // move it. It walks to a spot that sees the target, the nearest first,
    // as the healer does for the tank (HealerSight.h): the cast place round
    // the target (SafePlace.h, FindSafeCastPlace) within RangedStandYards,
    // safe of the packs outside the fight (KeepOut.h). With no safe spot,
    // it chases the target to RangedStandYards: the chase path ends in
    // sight. Not while the bot casts: a step would stop the cast. The rule
    // has its own name, so that Server.log shows the bot out of sight.
    Rule TargetSightRule(int priority);
    char const* const TargetSightRuleName = "get in sight of the target";

    // A rule of each damage bot (jvbastel/WOWAI#171): when a living enemy
    // attacks the bot and it is farther than AtTankYards from the living
    // tank, it walks to the tank, so that the tank can take the enemy off
    // it. It keeps hitting the focus once there. Not with an attack target
    // of the player.
    Rule WalkToTankRule(int priority);

    // jvbastel/WOWAI#194: the threat throttle (spec #188). An enemy goes
    // from the tank to a member whose threat on it is this ratio of the
    // tank's: in melee, and at range.
    float const MeleeTakeoverRatio = 1.1f;
    float const RangedTakeoverRatio = 1.3f;
    // A damage bot holds back on the focus at this share of its takeover
    // ratio, and starts again under the second one.
    float const ThreatStopShare = 0.9f;
    float const ThreatStartShare = 0.8f;

    // jvbastel/WOWAI#194: in a player-tank group with a living tank, the
    // bot holds back on enemy `enemy` now: its threat ratio on it
    // (BotView::GetThreatRatio) reached ThreatStopShare of `takeover`, and
    // has not fallen under ThreatStartShare of it since
    // (GroupCoordinator::HoldBackOnThreat). Notes the change. Never in a
    // bot-tank group: its warrior tank keeps threat with its own rules.
    // PickDamageTarget (RangedTakeoverRatio) and PickMeleeDamageTarget
    // (MeleeTakeoverRatio) skip the enemy healer and the focus while it
    // holds on them, and the healer's wand skips the focus. A loose enemy
    // that attacks the bot is no focus: the bot still fights it back.
    bool HoldsBackOnThreat(BotContext const& context, Guid enemy, float takeover);
}

#endif
