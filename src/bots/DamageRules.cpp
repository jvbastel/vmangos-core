#include "DamageRules.h"
#include "CommonRules.h"
#include "GroupRules.h"
#include "KeepOut.h"

#include <algorithm>
#include <vector>

namespace Bots
{
    namespace
    {
        // Outside a pull, a focus that hits a member other than the living
        // tank is not the tank's yet: the damage bots wait for the taunt,
        // while the taunt can come.
        bool FocusHitsAnotherMember(BotContext const& context, Guid focus)
        {
            if (context.group.GetPull().phase != PullPhase::None || context.group.IsTauntGivenUp(focus))
                return false;
            Guid const tank = context.group.GetTank();
            Guid const victim = context.view.GetVictim(focus);
            return IsAlive(context.view, tank) && victim != NoGuid && victim != tank
                && victim != context.view.GetSelf();
        }

        // The living enemies in the chase limit that hit a living group
        // member other than the living tank, the bot included, in the
        // order of the group members. It skips the
        // enemy `skip`. The tank holds its own attackers.
        std::vector<Guid> LooseEnemies(BotContext const& context, Guid skip)
        {
            Guid const tank = context.group.GetTank();
            bool const tankLives = IsAlive(context.view, tank);
            std::vector<Guid> enemies;
            for (Guid member : context.view.GetGroupMembers())
                if (IsAlive(context.view, member) && !(tankLives && member == tank))
                    for (Guid attacker : context.view.GetAttackers(member))
                        if (attacker != skip && IsAlive(context.view, attacker) && IsInChaseLimit(context, attacker)
                            && !Contains(enemies, attacker))
                            enemies.push_back(attacker);
            return enemies;
        }

        // Which other members count in AnotherMemberAttacks.
        enum class Members { All, LowerGuid };

        // True when a group member other than the bot and the player
        // attacks enemy `enemy`; with Members::LowerGuid, only a member whose
        // guid is lower than the bot's counts. The attacks of the player do
        // not count: in a scripted run, the test driver holds no enemy.
        bool AnotherMemberAttacks(BotContext const& context, Guid enemy, Members members = Members::All)
        {
            bool const lowerGuidOnly = members == Members::LowerGuid;
            Guid const self = context.view.GetSelf();
            for (Guid member : context.view.GetGroupMembers())
                if (member != self && member != context.commandPlayer && (!lowerGuidOnly || member < self)
                    && IsAlive(context.view, member) && context.view.GetVictim(member) == enemy)
                    return true;
            return false;
        }

        // The loose enemy of the bot, or NoGuid: the one that the bot
        // attacks, when no member with a lower guid attacks it too; else the
        // first that no member attacks; else the bot's own victim; else the
        // first. So the damage bots spread over the loose enemies, no enemy
        // stays lost, and two bots on one enemy do not swap back and forth.
        Guid PickLooseEnemy(BotContext const& context, Guid skip)
        {
            std::vector<Guid> const enemies = LooseEnemies(context, skip);
            if (enemies.empty())
                return NoGuid;
            Guid const own = context.view.GetVictim(context.view.GetSelf());
            bool const ownIsLoose = Contains(enemies, own);
            if (ownIsLoose && !AnotherMemberAttacks(context, own, Members::LowerGuid))
                return own;
            for (Guid enemy : enemies)
                if (!AnotherMemberAttacks(context, enemy))
                    return enemy;
            return ownIsLoose ? own : enemies.front();
        }
    }

    Guid PickDamageTarget(BotContext const& context)
    {
        if (IsAlive(context.view, context.attackTarget))
            return context.attackTarget;
        Guid const healer = PickEnemyHealer(context);
        if (healer != NoGuid)
            return healer;
        Guid const focus = context.group.GetFocusTarget();
        bool const waitForTaunt = IsAlive(context.view, focus) && FocusHitsAnotherMember(context, focus);
        Guid const loose = PickLooseEnemy(context, waitForTaunt ? focus : NoGuid);
        if (loose != NoGuid)
            return loose;
        if (IsAlive(context.view, focus) && TankHoldsThreat(context) && !waitForTaunt
            && IsInChaseLimit(context, focus))
            return focus;
        return NoGuid;
    }

    Guid PickMeleeDamageTarget(BotContext const& context)
    {
        Guid const target = PickDamageTarget(context);
        return target != NoGuid && InOtherPacksReach(context, context.view.GetPosition(target)) ? NoGuid : target;
    }

    bool TankHoldsThreat(BotContext const& context)
    {
        PullState const pull = context.group.GetPull();
        if (pull.phase == PullPhase::Holding && !pull.hasContact
            && context.view.GetVictim(pull.target) == pull.puller
            && context.view.GetDistanceBetween(pull.target, pull.puller) <= MeleeContactYards)
            context.group.NoteFocusContact(context.nowMs);
        return context.group.TankHoldsThreat(context.nowMs);
    }

    bool HasDamageTarget(BotContext const& context)
    {
        return PickDamageTarget(context) != NoGuid;
    }

    Rule CloseInRule(int priority)
    {
        Rule rule;
        rule.name = "run into casting range";
        rule.priority = priority;
        rule.trigger = [](BotContext const& context) {
            return HasDamageTarget(context) && context.view.GetSpellDistance(PickDamageTarget(context)) > CastingRangeYards;
        };
        rule.action.canDo = [](BotContext const&) { return true; };
        rule.action.run = [](BotContext const& context) {
            context.actions.ChaseToRange(PickDamageTarget(context), RangedStandYards);
        };
        return rule;
    }
}
