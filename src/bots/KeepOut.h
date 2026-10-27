#ifndef WOWAI_BOTS_KEEP_OUT_H
#define WOWAI_BOTS_KEEP_OUT_H

#include "BotView.h"

#include <cstdint>
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
}

#endif
