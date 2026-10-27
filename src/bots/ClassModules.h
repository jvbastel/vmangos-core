#ifndef WOWAI_BOTS_CLASS_MODULES_H
#define WOWAI_BOTS_CLASS_MODULES_H

#include "BotRecipe.h"
#include "Strategy.h"

#include <memory>
#include <vector>

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

    // The pet spells that the pet of a bot of class `botClass` casts on its
    // own (autocast), by the spell ID of the first rank. Empty for a class
    // without a pet.
    std::vector<SpellId> PetAutocastSpells(BotClass botClass);

    // The strategy stack of a bot, bottom first: the command strategy, the
    // member spot of a pull (MemberSpot.h, not for the tank), the rest after
    // a fight, the generic group rules (GroupRules.h), the class
    // and role strategy, and the death strategy (DeathAndRest.h). The boss
    // reactions come on top during a boss fight.
    StrategyStack MakeStrategyStack(BotClass botClass, BotRole role);
}

#endif
