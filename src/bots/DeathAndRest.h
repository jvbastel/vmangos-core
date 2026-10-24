#ifndef WOWAI_BOTS_DEATH_AND_REST_H
#define WOWAI_BOTS_DEATH_AND_REST_H

#include "Strategy.h"

#include <memory>

// Death, release, revive, and rest (step 8 of the version 1 build spec).
namespace Bots
{
    // A bot revives at its corpse when the leader is alive within this
    // distance of the corpse.
    float const ReviveNearLeaderYards = 30.0f;

    // The strategy of a dead bot, in the Death band: it outranks every other
    // rule, so a dead bot does nothing else. The player picks the moment of
    // release: when the leader becomes a ghost, the bot releases. A ghost
    // revives at its corpse when the leader is alive near the corpse. After
    // a fight that no living member can resurrect from, the bot revives at
    // its corpse at once. Otherwise the bot waits, for example for the
    // Priest's resurrect.
    std::shared_ptr<Strategy const> MakeDeathStrategy();

    // The rest after a fight, in the Command band over the corpse loot: a
    // bot below half of its health eats, a bot below half of its mana
    // drinks, and a bot that eats or drinks keeps resting until it is full.
    std::shared_ptr<Strategy const> MakeRestStrategy();

    // The group watch of one bot, before each think: it tells the
    // coordinator whether every member that the bot sees is dead (the wipe)
    // and whether the bot rests now.
    void WatchGroup(BotView const& view, GroupCoordinator& group);
}

#endif
