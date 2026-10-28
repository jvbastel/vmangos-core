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
        // jvbastel/WOWAI#194: lowers the Rogue's threat on its target, at
        // the threat stop (HoldsBackOnThreat, DamageRules.h).
        SpellId const Feint = 1966;

        // The melee damage bot: it attacks the damage target (DamageRules.h)
        // from behind when the target attacks another unit, builds combo
        // points with Sinister Strike, keeps Slice and Dice up, and spends
        // five combo points on Eviscerate. On a dying target it spends the
        // combo points it has, so that they are not lost.
        // jvbastel/WOWAI#194: at the threat stop (HoldsBackOnThreat,
        // DamageRules.h) it uses Feint when it can, else it stops its swing.
        std::shared_ptr<Strategy const> MakeMeleeStrategy();
    }
}

#endif
