#ifndef WOWAI_BOTS_KEEP_OUT_H
#define WOWAI_BOTS_KEEP_OUT_H

#include "BotView.h"
#include "Strategy.h"

#include <cstdint>
#include <memory>
#include <vector>

// The packs outside the fight (jvbastel/WOWAI#121): the living enemies that
// are in no combat, but the pack of the enemy that the group fights. A
// member that stands within the aggro distance and AggroMarginYards of one
// of them (PullsAnEnemy, SafePlace.h) may pull its pack into the fight.
namespace Bots
{
    // The enemies round a place that a member there could pull: more than
    // the aggro distance of any creature and the margin.
    float const KeepOutSearchYards = 50.0f;

    // The enemies outside the fight round `at`, that is: in no combat, and
    // not in the pack of `fightTarget` (SplitTargetPack, FightSpot.h).
    std::vector<NearEnemy> EnemiesOutsideTheFight(BotView const& view, Position const& at, Guid fightTarget);

    // The packs outside the fight that a member at `at` pulls. 0: a member
    // may stand at `at`.
    std::uint32_t PacksPulledAt(BotView const& view, Position const& at, Guid fightTarget);

    // The strategy "keep out": its rule KeepOutRule, for each bot, the tank
    // too, at Priority::Group + 82. While a pull holds, or the bot is in combat, a bot
    // that stands within the aggro distance and AggroMarginYards of an
    // enemy outside the fight of the group's focus steps out to a safe place
    // (FirstSafePlace, SafePlace.h). With no safe place the rule does not
    // fire: the bot keeps its fight rather than freeze. The one
    // exception: the puller that goes in to a caster (PullState::goesIn,
    // WarriorModule.h).
    std::shared_ptr<Strategy const> MakeKeepOutStrategy();
    char const* const KeepOutRule = "keep out of other packs";
}

#endif
