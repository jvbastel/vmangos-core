#include "CommonRules.h"
#include "SpellBook.h"

namespace Bots
{
    Guid PickSelf(BotContext const& context)
    {
        return context.view.GetSelf();
    }

    Guid PickFocusTarget(BotContext const& context)
    {
        return context.group.GetFocusTarget();
    }

    Guid PickPullTarget(BotContext const& context)
    {
        return context.group.GetPull().target;
    }

    Guid PickDamageTarget(BotContext const& context)
    {
        if (IsAlive(context.view, context.attackTarget))
            return context.attackTarget;
        Guid const focus = context.group.GetFocusTarget();
        if (IsAlive(context.view, focus) && context.group.TankHoldsThreat())
            return focus;
        for (Guid attacker : context.view.GetAttackers(context.view.GetSelf()))
            if (IsAlive(context.view, attacker))
                return attacker;
        return NoGuid;
    }

    bool IsAlive(BotView const& view, Guid unit)
    {
        return unit != NoGuid && view.GetHealth(unit).current > 0;
    }

    Rule CastRule(std::string name, int priority, Trigger trigger, SpellId firstRank, TargetPicker target)
    {
        Rule rule;
        rule.name = std::move(name);
        rule.priority = priority;
        rule.trigger = std::move(trigger);
        rule.action.canDo = [firstRank, target](BotContext const& context) {
            SpellId const rank = HighestKnownRank(context.view, firstRank);
            Guid const unit = target(context);
            return rank != NoSpell && unit != NoGuid && context.view.CanCast(rank, unit);
        };
        rule.action.run = [firstRank, target](BotContext const& context) {
            context.actions.CastSpell(HighestKnownRank(context.view, firstRank), target(context));
        };
        return rule;
    }

    Rule MeleeRule(std::string name, int priority, Trigger trigger, TargetPicker target)
    {
        Rule rule;
        rule.name = std::move(name);
        rule.priority = priority;
        rule.trigger = std::move(trigger);
        rule.action.canDo = [target](BotContext const& context) { return IsAlive(context.view, target(context)); };
        rule.action.run = [target](BotContext const& context) {
            Guid const unit = target(context);
            context.actions.Attack(unit);
            context.actions.Chase(unit);
        };
        return rule;
    }
}
