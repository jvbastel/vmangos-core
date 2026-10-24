#include "DamageRules.h"
#include "CommonRules.h"
#include "GroupRules.h"

namespace Bots
{
    Guid PickDamageTarget(BotContext const& context)
    {
        if (IsAlive(context.view, context.attackTarget))
            return context.attackTarget;
        Guid const healer = PickEnemyHealer(context);
        if (healer != NoGuid)
            return healer;
        Guid const focus = context.group.GetFocusTarget();
        if (IsAlive(context.view, focus) && context.group.TankHoldsThreat() && IsInChaseLimit(context, focus))
            return focus;
        for (Guid attacker : context.view.GetAttackers(context.view.GetSelf()))
            if (IsAlive(context.view, attacker))
                return attacker;
        return NoGuid;
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
