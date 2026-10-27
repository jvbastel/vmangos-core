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
        SpellId const DemoralizingShout = 1160;
        SpellId const ChallengingShout = 1161;
        SpellId const ShieldBlock = 2565;
        // Never cast before level 51: with no Tactical Mastery, the stance
        // change they need drops all rage (jvbastel/WOWAI#102).
        SpellId const ThunderClap = 6343;
        SpellId const MockingBlow = 694;
        // Interrupts a cast, with a shield: a generic group rule (GroupRules.h).
        SpellId const ShieldBash = 72;

        // The names of the tank rules that the game layer logs more for
        // (BotAI.cpp): one name for the rule and its log.
        char const* const NoteFightSpotRule = "note the fight spot";
        char const* const WaitForTheGroupRule = "wait for the group";

        // The tank: it pulls the pull target of the group coordinator when it
        // is the puller, holds threat on the focus target, and fights back an
        // enemy that attacks it. It never picks a target of its own.
        // The pull: it picks a fight spot on its cleared way (FightSpot.h),
        // shoots the target with its ranged weapon (Ranged.h) from where it
        // stands or from its shoot spot, and walks back to the fight spot,
        // where it holds the pack. With no clean fight spot it warns in
        // party chat. With no ranged weapon or no ammo, or too near for a
        // shot, it runs in, hits the target once, and walks back. Never
        // Charge: it stuns the target in place. Out of combat it tops up its ammo for free.
        // The opener: Defensive Stance at once (after a Charge too: with no
        // Tactical Mastery the stance change drops all rage), Bloodrage, then
        // Demoralizing Shout on the enemies near. It shouts again for an
        // enemy in the fight that lacks the debuff (jvbastel/WOWAI#114).
        // It never casts Thunder Clap or Mocking Blow.
        // A caster pack (jvbastel/WOWAI#121): the tank waits at its fight
        // spot for a caster that stays away, then goes in (CasterComeMs,
        // GroupCoordinator.h).
        // It warns the moment another pack joins the fight of its pull,
        // once a pack, whatever brought it.
        std::shared_ptr<Strategy const> MakeTankStrategy();
    }
}

#endif
