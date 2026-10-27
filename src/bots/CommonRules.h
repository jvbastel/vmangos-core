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
    // True when the bot sees the dead body of unit `unit`. Not the opposite
    // of IsAlive: a unit that the bot does not find (on another map, so with
    // no health) is neither alive nor seen dead.
    bool SeenDead(BotView const& view, Guid unit);

    // True when a living member of the bot's group that the bot can see is
    // in combat, the bot too. False after the fight.
    bool IsGroupInCombat(BotView const& view);

    // True when unit `unit` is a member of the bot's group.
    bool IsGroupMember(BotView const& view, Guid unit);

    // True when a living enemy that attacks the bot is at most `yards` from
    // it: a melee hit, for Shield Block (WarriorModule.cpp) or Frost Nova
    // (MageModule.cpp).
    bool IsHitInMelee(BotContext const& context, float yards);

    // True when the bot follows a living group member other than the
    // player, such as the healer on its way to a hurt member (PriestModule).
    bool FollowsAMember(BotContext const& context);
    // `current` as a percent of `max`, rounded down. 0 when `max` is 0.
    std::uint32_t Percent(std::uint32_t current, std::uint32_t max);

    // A rule that casts the highest known rank of the spell whose first rank
    // is `firstRank` that the level of the picked unit allows, on that unit
    // (SpellBook.h, CastableRank). The bot can do it when it knows such a
    // rank and can cast it now.
    Rule CastRule(std::string name, int priority, Trigger trigger, SpellId firstRank, TargetPicker target);

    // A rule that starts the melee attack of the picked unit and runs to it.
    Rule MeleeRule(std::string name, int priority, Trigger trigger, TargetPicker target);
}

#endif
