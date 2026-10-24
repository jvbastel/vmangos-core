#ifndef WOWAI_BOTS_PRIEST_MODULE_H
#define WOWAI_BOTS_PRIEST_MODULE_H

#include "Strategy.h"

#include <memory>

// The Priest class module. Version 1 has the healer role only.
namespace Bots
{
    namespace Priest
    {
        // The spells, by the spell ID of the first rank.
        SpellId const LesserHeal = 2050;
        SpellId const Heal = 2054;
        SpellId const Renew = 139;
        SpellId const PowerWordShield = 17;
        SpellId const PowerWordFortitude = 1243;
        SpellId const InnerFire = 588;
        SpellId const Fade = 586;
        // Dispels, from level 14 and 18: generic group rules (GroupRules.h).
        SpellId const CureDisease = 528;
        SpellId const DispelMagic = 527;
        SpellId const Resurrection = 2006;
        // The debuff of Power Word: Shield: no new shield while it lasts.
        SpellId const WeakenedSoul = 6788;

        // The healer: it keeps the members of its group alive and only heals.
        // It never attacks, not even an enemy that attacks it. At level 16 it
        // knows no dispel, so it heals through every debuff. After a fight
        // it resurrects the dead members that have not released.
        std::shared_ptr<Strategy const> MakeHealerStrategy();
    }
}

#endif
