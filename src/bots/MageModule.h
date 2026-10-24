#ifndef WOWAI_BOTS_MAGE_MODULE_H
#define WOWAI_BOTS_MAGE_MODULE_H

#include "Strategy.h"

#include <memory>

// The Mage class module. Version 1 has the frost damage role (ranged) only.
// The test driver takes the player's seat as this Mage.
namespace Bots
{
    namespace Mage
    {
        // The spells, by the spell ID of the first rank.
        SpellId const Fireball = 133;
        SpellId const Frostbolt = 116;
        SpellId const FireBlast = 2136;
        SpellId const FrostNova = 122;
        SpellId const IceBarrier = 11426; // A frost talent.
        SpellId const Evocation = 12051;
        SpellId const FrostArmor = 168;
        SpellId const IceArmor = 7302; // Replaces Frost Armor from level 30.
        SpellId const ArcaneIntellect = 1459;

        // The frost damage strategy: it attacks the focus target with the
        // targeting rules of all damage bots (DamageRules.h).
        std::shared_ptr<Strategy const> MakeFrostStrategy();
    }
}

#endif
