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
// - Chase limit: a damage bot chases an enemy only up to ChaseLimitYards from
//   the tank, and then comes back to the tank. Only the attack target of the
//   player has no limit.
// - Dispel: a bot that knows a dispel spell removes a debuff of that kind
//   from a group member. At level 16 no bot of the group knows one.
namespace Bots
{
    // The strategy of the generic group rules, "group rules", for a bot of
    // role `role`.
    std::shared_ptr<Strategy const> MakeGroupStrategy(BotRole role);

    // The living enemies in the fight of the group: the focus target and the
    // units that attack a group member.
    std::vector<Guid> EnemiesInFight(BotContext const& context);

    // The first living attacker of a group member, in the order of the group
    // members (the bot first), or NoGuid. It skips the attackers of member
    // `skipMember` and the enemy `skipEnemy`.
    Guid PickMemberAttacker(BotContext const& context, Guid skipMember = NoGuid, Guid skipEnemy = NoGuid);

    // A damage bot attacks an enemy only while that enemy is at most this far
    // from the tank.
    float const ChaseLimitYards = 20.0f;

    // True when unit `unit` is in the chase limit: it is at most
    // ChaseLimitYards from the tank, or the group has no living tank.
    bool IsInChaseLimit(BotContext const& context, Guid unit);

    // An enemy healer of the group that is alive, in the fight, and in the
    // chase limit, or NoGuid.
    Guid PickEnemyHealer(BotContext const& context);
}

#endif
