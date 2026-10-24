#ifndef WOWAI_BOTS_CLASS_MODULES_H
#define WOWAI_BOTS_CLASS_MODULES_H

#include "BotRecipe.h"
#include "Strategy.h"

#include <memory>

// The class modules and the strategy stack of a new bot.
//
// A class module supplies a strategy for each role that its class plays, in
// its own file (for example WarriorModule.cpp). It names spells by the spell
// ID of the first rank (CommonRules.h and SpellBook.h). To add a module: a
// row in the table of ClassModules.cpp.
namespace Bots
{
    // The strategy of the command state: a bot that follows the player keeps
    // following out of combat, for example after a fight or a teleport. Out
    // of combat, the bot first loots the corpses near it (rule "loot a
    // corpse", over "follow the player").
    std::shared_ptr<Strategy const> MakeCommandStrategy();

    // The class and role strategy, or null when no class module plays the
    // role.
    std::shared_ptr<Strategy const> MakeClassStrategy(BotClass botClass, BotRole role);

    // The strategy stack of a bot, bottom first: the command strategy, then
    // the class and role strategy. The generic group rules and the boss
    // reactions come in later tickets.
    StrategyStack MakeStrategyStack(BotClass botClass, BotRole role);
}

#endif
