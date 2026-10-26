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

    // True when the bot can see unit `unit` and the unit is alive: not dead
    // and no ghost.
    bool IsAlive(BotView const& view, Guid unit);

    // True when a living member of the bot's group that the bot can see is
    // in combat, the bot too. False after the fight.
    bool IsGroupInCombat(BotView const& view);

    // A rule that casts the highest known rank of the spell whose first rank
    // is `firstRank` that the level of the picked unit allows, on that unit
    // (SpellBook.h, CastableRank). The bot can do it when it knows such a
    // rank and can cast it now.
    Rule CastRule(std::string name, int priority, Trigger trigger, SpellId firstRank, TargetPicker target);

    // A rule that starts the melee attack of the picked unit and runs to it.
    Rule MeleeRule(std::string name, int priority, Trigger trigger, TargetPicker target);
}

#endif
