#ifndef WOWAI_BOTS_GROUP_RULES_H
#define WOWAI_BOTS_GROUP_RULES_H

#include "BotRecipe.h"
#include "Strategy.h"

#include <memory>
#include <vector>

// The generic group rules (spec 7, Generic rules). They apply in every fight,
// for each bot that can do them, in the Group band over the class rules:
// - Interrupts: a bot that knows Kick or Shield Bash (with a shield) stops a
//   cast of an enemy in the fight, heals first, then harmful spells. The
//   interrupt claims of the group coordinator make sure that two bots never
//   interrupt the same cast. A bot whose interrupt is on cooldown leaves the
//   next cast to the other bot, so the bots take turns.
// - Enemy healers: an enemy that casts a heal becomes an enemy healer of the
//   group. The damage bots attack it first (PickDamageTarget), until it dies.
// - Chase limit: a damage bot chases an enemy only up to ChaseEnemyYards from
//   the tank, or from the other member that the enemy hits, and then comes
//   back to the tank. Nor does it chase an enemy that stands where a fight
//   pulls a pack out of the fight (jvbastel/WOWAI#121): the tank leads it
//   away first. Only the attack target of the player has no limit.
// - Dispel: a bot that knows a dispel spell removes a debuff of that kind
//   from a group member. At level 16 no bot of the group knows one.
namespace Bots
{
    // The strategy of the generic group rules, "group rules", for a bot of
    // role `role`.
    std::shared_ptr<Strategy const> MakeGroupStrategy(BotRole role);

    // Whether `firstRank`, the spell ID of the first rank of a spell, is an
    // interrupt spell of the group rules (Kick, Shield Bash).
    bool IsInterruptSpell(SpellId firstRank);

    // The living enemies in the fight of the group: the focus target and the
    // units that attack a group member.
    std::vector<Guid> EnemiesInFight(BotContext const& context);

    // The first living attacker of a living group member, in the order of the group
    // members (the bot first), or NoGuid. It skips the attackers of member
    // `skipMember` and the enemy `skipEnemy`.
    Guid PickMemberAttacker(BotContext const& context, Guid skipMember = NoGuid, Guid skipEnemy = NoGuid);

    // The chase limit of spec section 10: no bot chases more than this far
    // from the tank. The run counts each chase beyond it (ChaseWatch.h), and
    // writes it into the result file for wowai/test-realm/runs.py.
    float const ChaseLimitYards = 25.0f;
    // A damage bot attacks an enemy only while that enemy is at most this far
    // from the tank: a melee bot at the enemy then stays within
    // ChaseLimitYards of the tank.
    float const ChaseEnemyYards = ChaseLimitYards - 5.0f;

    // True when unit `unit` is in the chase limit: it is at most
    // ChaseEnemyYards from the tank, or the group has no living tank, or it
    // hits a living group member other than the bot and is at most
    // ChaseEnemyYards from it. A fleeing enemy leaves that member behind.
    // An enemy on the bot itself has the limit of the tank: it follows the
    // bot to the tank. Never an enemy that stands within the aggro distance
    // and the margin of an enemy out of the fight (PacksPulledAt, FightSpot.h).
    bool IsInChaseLimit(BotContext const& context, Guid unit);

    // An enemy healer of the group that is alive, in the fight, and in the
    // chase limit, or NoGuid.
    Guid PickEnemyHealer(BotContext const& context);
}

#endif
