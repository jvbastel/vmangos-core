#ifndef WOWAI_BOTS_WARLOCK_MODULE_H
#define WOWAI_BOTS_WARLOCK_MODULE_H

#include "Strategy.h"

#include <memory>

// The Warlock class module. Version 1 has the ranged damage role only, with
// the Affliction talent build (warlock_ranged) and the Imp.
namespace Bots
{
    namespace Warlock
    {
        // The spells, by the spell ID of the first rank.
        SpellId const SummonImp = 688;
        SpellId const DemonSkin = 687;
        SpellId const DemonArmor = 706;
        SpellId const ShadowBolt = 686;
        SpellId const Immolate = 348;
        SpellId const Corruption = 172;
        SpellId const CurseOfAgony = 980;
        SpellId const LifeTap = 1454;

        // The spells of the Imp that it casts on its own (autocast). Not
        // Phase Shift: the Imp cannot attack in it.
        SpellId const Firebolt = 3110;
        SpellId const BloodPact = 6307;

        // The ranged damage bot: out of combat it summons its Imp and keeps
        // its armor up. In the fight it sends the Imp to the damage target
        // (DamageRules.h), runs into casting range of the target, puts its damage
        // over time spells on it, and casts Shadow Bolt.
        std::shared_ptr<Strategy const> MakeRangedStrategy();
    }
}

#endif
