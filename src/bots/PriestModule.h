#ifndef WOWAI_BOTS_PRIEST_MODULE_H
#define WOWAI_BOTS_PRIEST_MODULE_H

#include "Ranged.h"
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
        // jvbastel/WOWAI#135: from level 20, for a member about to die.
        SpellId const FlashHeal = 2061;
        // jvbastel/WOWAI#136: from level 40, the big heal.
        SpellId const GreaterHeal = 2060;
        SpellId const Renew = 139;
        // jvbastel/WOWAI#133: the wand shot between heals. It repeats on its
        // own until a cast stops it.
        SpellId const Shoot = WandShoot;
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

        // The healer: it keeps the members of its group alive. It attacks
        // only with its wand, and only the focus target (jvbastel/WOWAI#133):
        // in a fight with nobody to heal, once the tank holds threat, it
        // shoots between heals. Each heal comes first. It never fights back
        // an enemy that attacks it: it fades and shields itself. At level 16 it
        // knows no dispel, so it heals through every debuff. After a fight
        // it resurrects each dead member in reach that has not released.
        // Then it walks to a corpse out of reach, to a safe place in cast
        // range (SafePlace.h). With no safe place it does
        // not go: it gives the resurrect up, and the dead member runs back.
        std::shared_ptr<Strategy const> MakeHealerStrategy();
    }
}

#endif
