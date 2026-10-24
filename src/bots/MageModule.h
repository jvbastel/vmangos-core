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

        // The frost damage strategy: it attacks the focus target with the
        // targeting rules of all damage bots (DamageRules.h).
        std::shared_ptr<Strategy const> MakeFrostStrategy();
    }
}

#endif
