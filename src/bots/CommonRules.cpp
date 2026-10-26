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

    std::uint32_t Percent(std::uint32_t current, std::uint32_t max)
    {
        return max == 0 ? 0 : current * 100 / max;
    }

    bool IsAlive(BotView const& view, Guid unit)
    {
        return unit != NoGuid && view.GetHealth(unit).current > 0 && view.GetLifeState(unit) == LifeState::Alive;
    }

    bool IsGroupInCombat(BotView const& view)
    {
        for (Guid member : view.GetGroupMembers())
            if (IsAlive(view, member) && view.IsInCombat(member))
                return true;
        return false;
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
