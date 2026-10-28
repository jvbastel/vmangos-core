#ifndef WOWAI_BOTS_RANGED_H
#define WOWAI_BOTS_RANGED_H

#include "BotTypes.h"

#include <cstdint>

// The ranged weapon of a bot and its ammo, for the ranged pull of the tank
// (WarriorModule.h) and the wand of the Priest healer (PriestModule.h). The
// bot system keeps the ammo topped up for free: bots never buy ammo. A wand
// uses none.
namespace Bots
{
    class BotView;

    enum class RangedKind
    {
        None, // No ranged weapon.
        Thrown,
        Bow,
        Gun,
        Crossbow,
        Wand, // jvbastel/WOWAI#133: no ammo, and no top-up.
    };

    // The ranged weapon that the bot wears.
    struct RangedWeapon
    {
        RangedKind kind = RangedKind::None;
        // The item entry of the weapon, or 0.
        std::uint32_t entry = 0;
        // The ammo the bot uses: for a thrown weapon, the weapon itself
        // (a stack that each throw uses up); else the arrows or bullets of
        // the ammo slot (0 for none).
        std::uint32_t ammoEntry = 0;
        // How many of the ammo the bot has: the thrown stack, or the arrows
        // or bullets of `ammoEntry` in its bags.
        std::uint32_t ammoCount = 0;
    };

    // The bot tops its ammo up to this many.
    std::uint32_t const AmmoStack = 200;
    // It tops it up when it has fewer.
    std::uint32_t const LowAmmo = 100;

    // Bring the ammo up to `count` of item `entry`. The adapter adds the
    // missing items silently: to the thrown stack, or to the bags, with
    // `entry` as the ammo of the ammo slot. `count` 0: nothing to do.
    struct AmmoTopUp
    {
        std::uint32_t entry = 0;
        std::uint32_t count = 0;
    };

    // The vendor arrows (bow, crossbow) or bullets (gun) of the highest
    // required level that `level` allows. 0 for a thrown weapon and none.
    std::uint32_t AmmoFor(RangedKind kind, std::uint32_t level);

    // The top-up that a bot of level `level` needs for `weapon`: the best
    // ammo of its level, when it uses other ammo or has less than
    // LowAmmo of it. A thrown weapon tops up its own stack. A wand needs
    // no top-up.
    AmmoTopUp PlanAmmoTopUp(RangedWeapon const& weapon, std::uint32_t level);

    // jvbastel/WOWAI#133: Shoot, the shot of a wand (spell range 4).
    SpellId const WandShoot = 5019;

    // The spell that fires the weapon: Throw, Shoot Bow, Shoot Gun, Shoot
    // Crossbow, or Shoot (a wand, WandShoot). NoSpell for none.
    SpellId ShootSpell(RangedKind kind);

    // The bot can fire the weapon: it wears one and has ammo for it. A wand
    // needs no ammo.
    bool CanFire(RangedWeapon const& weapon);

    // The bot can fire the ranged weapon that it wears (CanFire), and knows
    // its shoot spell (ShootSpell).
    bool CanShoot(BotView const& view);

    // The one "in shot" rule of the ranged pull (WarriorModule.h, the
    // fight spot search of FightSpot.h). The shoot spells reach from
    // ShotMinYards to ShotMaxYards (spell range 74), measured as spell
    // ranges measure them: between the combat reaches
    // (BotView::GetSpellDistanceFrom), so no body margin is guessed.
    float const ShotMinYards = 8.0f;
    float const ShotMaxYards = 30.0f;

    enum class ShotReach
    {
        TooNear, // Nearer than ShotMinYards: no shot.
        InRange,
        TooFar, // Farther than ShotMaxYards.
    };

    // Where a target `spellYards` away (spell distance) is for a shot.
    ShotReach ShotReachAt(float spellYards);

    // A bot at `at` can shoot unit `target`: from `minYards` (spell
    // distance) to ShotMaxYards, and in sight.
    bool InShotFrom(BotView const& view, Position const& at, Guid target, float minYards = ShotMinYards);

    // jvbastel/WOWAI#133: the bot can shoot unit `target` with a wand from
    // where it stands (InShotFrom). WandShoot reaches up to ShotMaxYards
    // with no minimum, unlike the other shoot spells.
    bool InWandShot(BotView const& view, Guid target);

    // The length of a step of the path search of FindShotOnPath.
    float const ShotPathStepYards = 1.0f;

    // The first point of the bot's ground path to unit `target`
    // (BotView::FindGroundPath), checked each ShotPathStepYards, from which
    // it can shoot the target (InShotFrom): where a bot below a ledge first
    // sees a target on it (jvbastel/WOWAI#122). The core's own caster chase
    // does not find it: it cuts its path with the target's sight of the
    // bot, which a ledge edge can give while the bot sees nothing. No safe
    // place check: the path may pull enemies. False with no path, or no
    // shot from any point of it.
    bool FindShotOnPath(BotView const& view, Guid target, Position& spot);
}

#endif
