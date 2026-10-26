#ifndef WOWAI_BOTS_DEATH_AND_REST_H
#define WOWAI_BOTS_DEATH_AND_REST_H

#include "Strategy.h"

#include <memory>

// Death, release, revive, and rest (step 8 of the version 1 build spec).
namespace Bots
{
    // The strategy of a dead bot, in the Death band: it outranks every other
    // rule, so a dead bot does nothing else. No bot ever revives at its
    // corpse on its own. A dead bot comes back as a player does: a party
    // member resurrects it (the Priest's resurrect, at the corpse), or it
    // releases and runs back as a ghost from the graveyard to the dungeon
    // portal (BotActions::WalkAsGhost), and is alive again at the entrance.
    // The player picks the moment of release: when the leader becomes a
    // ghost, the bot releases. Else a dead bot waits for the Priest's
    // resurrect. It releases after the fight when that resurrect cannot
    // come: no living member that it sees can resurrect, or the Priest gave
    // it up (GroupCoordinator::GiveUpResurrect). In a wipe it waits for the
    // leader. A scripted run cuts the ghost run short (spec section 10).
    std::shared_ptr<Strategy const> MakeDeathStrategy();

    // The rest after a fight, in the Command band over the corpse loot: a
    // bot below half of its health eats, a bot below half of its mana
    // drinks, and a bot that eats or drinks keeps resting until it is full.
    std::shared_ptr<Strategy const> MakeRestStrategy();

    // True while the meal of the bot still fills it: it eats and its health
    // is not full, or it drinks and its mana is not full. A bot that is full
    // again does not rest, even while its food or drink aura lasts, so it
    // stands up and the group does not wait for it.
    bool RestsNow(BotView const& view);

    // The group watch of one bot, before each think: it tells the
    // coordinator whether every member that the bot sees is dead (the wipe)
    // and whether the bot rests now. A bot that lives again clears its
    // given-up resurrect.
    void WatchGroup(BotView const& view, GroupCoordinator& group);

    // The rest part of the group watch: whether the bot rests now, and the
    // end of its rest until full. The game layer runs it at each update of
    // the bot too, so the coordinator sees a meal start or end at once and
    // not one think later.
    void WatchRest(BotView const& view, GroupCoordinator& group);
}

#endif
