#ifndef WOWAI_BOTS_WARRIOR_MODULE_H
#define WOWAI_BOTS_WARRIOR_MODULE_H

#include "Strategy.h"

#include <memory>

// The Warrior class module. Version 1 has the tank role only.
namespace Bots
{
    namespace Warrior
    {
        // The spells, by the spell ID of the first rank.
        SpellId const BattleStance = 2457;
        SpellId const DefensiveStance = 71;
        SpellId const Charge = 100;
        SpellId const Taunt = 355;
        SpellId const HeroicStrike = 78;
        SpellId const SunderArmor = 7386;
        SpellId const Revenge = 6572;
        SpellId const BattleShout = 6673;
        SpellId const Bloodrage = 2687;
        // Interrupts a cast, with a shield: a generic group rule (GroupRules.h).
        SpellId const ShieldBash = 72;

        // The tank: it pulls the pull target of the group coordinator when it
        // is the puller, holds threat on the focus target, and fights back an
        // enemy that attacks it. It never picks a target of its own.
        // The pull: from where it stands, with its ranged weapon (Ranged.h),
        // so that the pack comes to it there. With no ranged weapon or no
        // ammo, or too near for a shot, it runs in, hits the target once,
        // and walks back to where it started. Never Charge: it stuns the
        // target in place. Out of combat it tops up its ammo for free.
        std::shared_ptr<Strategy const> MakeTankStrategy();
    }
}

#endif
