#ifndef WOWAI_BOTS_COMMON_RULES_H
#define WOWAI_BOTS_COMMON_RULES_H

#include "SpellBook.h"
#include "Strategy.h"

// Small parts that the class modules share to build their rules.
namespace Bots
{
    // Two units this near are in melee contact: the pull target and the
    // tank for the hold (DamageRules.h, jvbastel/WOWAI#127), the tank and
    // the enemy it melees or the caster that stays away (WarriorModule.cpp).
    float const MeleeContactYards = 5.0f;

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

    // The living units of `units` that are at most `yards` from the bot
    // (BotView::GetDistance), in their order.
    std::vector<Guid> UnitsWithin(BotView const& view, std::vector<Guid> const& units, float yards);

    // True when a living enemy that attacks the bot is at most `yards` from
    // it: a melee hit, for Shield Block (WarriorModule.cpp).
    bool IsHitInMelee(BotContext const& context, float yards);

    // jvbastel/WOWAI#143: an enemy this near (between the edges, as
    // BotView::GetDistance and GetDistanceBetween count) to the member that
    // it attacks hits it in melee. Wider than MeleeContactYards: a big enemy
    // hits from farther. A caster that attacks from range stands far beyond
    // it.
    float const MeleeHitYards = 8.0f;

    // The living enemies that attack member `member` and are at most
    // MeleeHitYards from it: the loose enemies on a member other than the
    // tank, for the peel nova (MageModule.cpp). The bot itself as `member`
    // uses BotView::GetDistance.
    std::vector<Guid> EnemiesHittingInMelee(BotView const& view, Guid member);

    // True when the bot follows a living group member other than the
    // player, such as the healer on its way to a hurt member (PriestModule).
    bool FollowsAMember(BotContext const& context);
    // `current` as a percent of `max`, rounded down. 0 when `max` is 0.
    std::uint32_t Percent(std::uint32_t current, std::uint32_t max);

    // Picks the rank of the spell whose first rank is `firstRank` that the
    // bot casts on unit `target` now, or NoSpell (SpellBook.h).
    using RankChoice = std::function<SpellId(BotView const& view, SpellId firstRank, Guid target)>;

    // A rule that casts the rank of the spell whose first rank is
    // `firstRank` that `rank` picks, on the picked unit. By default the
    // highest known rank that the level of the unit allows (SpellBook.h,
    // CastableRank). A heal passes CastableHealRank (jvbastel/WOWAI#134).
    // The bot can do it when `rank` picks a rank.
    Rule CastRule(std::string name, int priority, Trigger trigger, SpellId firstRank, TargetPicker target,
        RankChoice rank = CastableRank);

    // A rule that starts the melee attack of the picked unit and runs to it.
    Rule MeleeRule(std::string name, int priority, Trigger trigger, TargetPicker target);
}

#endif
