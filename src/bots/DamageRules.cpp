#include "DamageRules.h"
#include "CommonRules.h"

namespace Bots
{
    bool MayAttackFocus(BotContext const& context)
    {
        return IsAlive(context.view, context.group.GetFocusTarget())
            && context.group.GetPull().phase != PullPhase::Pulling;
    }

    Rule CloseInRule(int priority)
    {
        Rule rule;
        rule.name = "run into casting range";
        rule.priority = priority;
        rule.trigger = [](BotContext const& context) {
            return MayAttackFocus(context) && context.view.GetDistance(PickFocusTarget(context)) > CastingRangeYards;
        };
        rule.action.canDo = [](BotContext const&) { return true; };
        rule.action.run = [](BotContext const& context) {
            context.actions.ChaseToRange(PickFocusTarget(context), RangedStandYards);
        };
        return rule;
    }
}
