#include "DamageRules.h"
#include "CommonRules.h"
#include "GroupRules.h"

namespace Bots
{
    namespace
    {
        // Outside a pull, a focus that hits a member other than the living
        // tank is not the tank's yet: the damage bots wait for the taunt.
        bool FocusHitsAnotherMember(BotContext const& context, Guid focus)
        {
            if (context.group.GetPull().phase != PullPhase::None)
                return false;
            Guid const tank = context.group.GetTank();
            Guid const victim = context.view.GetVictim(focus);
            return IsAlive(context.view, tank) && victim != NoGuid && victim != tank
                && victim != context.view.GetSelf();
        }

        // The first living enemy in the chase limit that hits a living group
        // member, the bot first. It skips the enemy `skip`.
        Guid PickMemberAttackerInLimit(BotContext const& context, Guid skip)
        {
            for (Guid member : context.view.GetGroupMembers())
                if (IsAlive(context.view, member))
                    for (Guid attacker : context.view.GetAttackers(member))
                        if (attacker != skip && IsAlive(context.view, attacker) && IsInChaseLimit(context, attacker))
                            return attacker;
            return NoGuid;
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
        if (IsAlive(context.view, focus) && context.group.TankHoldsThreat() && !waitForTaunt
            && IsInChaseLimit(context, focus))
            return focus;
        return PickMemberAttackerInLimit(context, waitForTaunt ? focus : NoGuid);
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
            return HasDamageTarget(context) && context.view.GetDistance(PickDamageTarget(context)) > CastingRangeYards;
        };
        rule.action.canDo = [](BotContext const&) { return true; };
        rule.action.run = [](BotContext const& context) {
            context.actions.ChaseToRange(PickDamageTarget(context), RangedStandYards);
        };
        return rule;
    }
}
