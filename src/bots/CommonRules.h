#ifndef WOWAI_BOTS_COMMON_RULES_H
#define WOWAI_BOTS_COMMON_RULES_H

#include "Strategy.h"

// Small parts that the class modules share to build their rules.
namespace Bots
{
    // Picks the unit of a rule on one think, for example the focus target.
    using TargetPicker = std::function<Guid(BotContext const&)>;

    Guid PickSelf(BotContext const& context);
    Guid PickFocusTarget(BotContext const& context);
    Guid PickPullTarget(BotContext const& context);

    // The targeting rules of the damage bots (melee and ranged), or NoGuid:
    // 1. the attack target of the player (`attack`), at once;
    // 2. the focus target, once the tank holds threat on it;
    // 3. an enemy that attacks the bot itself.
    Guid PickDamageTarget(BotContext const& context);

    // True when the bot can see unit `unit` and the unit is alive.
    bool IsAlive(BotView const& view, Guid unit);

    // A rule that casts the highest known rank of the spell whose first rank
    // is `firstRank` on the picked unit. The bot can do it when it knows a
    // rank and BotView::CanCast allows it.
    Rule CastRule(std::string name, int priority, Trigger trigger, SpellId firstRank, TargetPicker target);

    // A rule that starts the melee attack of the picked unit and runs to it.
    Rule MeleeRule(std::string name, int priority, Trigger trigger, TargetPicker target);
}

#endif
