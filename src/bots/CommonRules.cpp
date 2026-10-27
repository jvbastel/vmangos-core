#include "CommonRules.h"
#include "SpellBook.h"

#include <algorithm>
#include <vector>

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

    bool SeenDead(BotView const& view, Guid unit)
    {
        Health const health = view.GetHealth(unit);
        return health.max > 0 && health.current == 0;
    }

    bool IsGroupInCombat(BotView const& view)
    {
        for (Guid member : view.GetGroupMembers())
            if (IsAlive(view, member) && view.IsInCombat(member))
                return true;
        return false;
    }

    bool IsGroupMember(BotView const& view, Guid unit)
    {
        std::vector<Guid> const members = view.GetGroupMembers();
        return unit != NoGuid && std::find(members.begin(), members.end(), unit) != members.end();
    }

    bool IsHitInMelee(BotContext const& context, float yards)
    {
        for (Guid attacker : context.view.GetAttackers(context.view.GetSelf()))
            if (IsAlive(context.view, attacker) && context.view.GetDistance(attacker) <= yards)
                return true;
        return false;
    }

    bool FollowsAMember(BotContext const& context)
    {
        Guid const target = context.view.GetFollowTarget();
        return target != context.commandPlayer && IsAlive(context.view, target) && IsGroupMember(context.view, target);
    }

    Rule CastRule(std::string name, int priority, Trigger trigger, SpellId firstRank, TargetPicker target)
    {
        Rule rule;
        rule.name = std::move(name);
        rule.priority = priority;
        rule.trigger = std::move(trigger);
        rule.action.canDo = [firstRank, target](BotContext const& context) {
            Guid const unit = target(context);
            return unit != NoGuid && CastableRank(context.view, firstRank, unit) != NoSpell;
        };
        rule.action.run = [firstRank, target](BotContext const& context) {
            Guid const unit = target(context);
            context.actions.CastSpell(RankForTarget(context.view, firstRank, unit), unit);
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
