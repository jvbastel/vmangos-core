#ifndef WOWAI_BOTS_ROGUE_MODULE_H
#define WOWAI_BOTS_ROGUE_MODULE_H

#include "Strategy.h"

#include <memory>

// The Rogue class module. Version 1 has the melee damage role only, with
// swords (the rogue_melee talent build): no Backstab.
namespace Bots
{
    namespace Rogue
    {
        // The spells, by the spell ID of the first rank.
        SpellId const SinisterStrike = 1752;
        SpellId const Eviscerate = 2098;
        SpellId const SliceAndDice = 5171;
        // Interrupts a cast: a generic group rule (GroupRules.h).
        SpellId const Kick = 1766;

        // The melee damage bot: it attacks the damage target (DamageRules.h)
        // from behind when the target attacks another unit, builds combo
        // points with Sinister Strike, keeps Slice and Dice up, and spends
        // five combo points on Eviscerate.
        std::shared_ptr<Strategy const> MakeMeleeStrategy();
    }
}

#endif
