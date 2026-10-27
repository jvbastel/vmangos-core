#ifndef WOWAI_BOTS_RANGED_H
#define WOWAI_BOTS_RANGED_H

#include "BotTypes.h"

#include <cstdint>

// The ranged weapon of a bot and its ammo, for the ranged pull of the tank
// (WarriorModule.h). The bot system keeps the ammo topped up for free: bots
// never buy ammo.
namespace Bots
{
    enum class RangedKind
    {
        None, // No ranged weapon, or a wand.
        Thrown,
        Bow,
        Gun,
        Crossbow,
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
    // LowAmmo of it. A thrown weapon tops up its own stack.
    AmmoTopUp PlanAmmoTopUp(RangedWeapon const& weapon, std::uint32_t level);

    // The spell that fires the weapon: Throw, Shoot Bow, Shoot Gun, or
    // Shoot Crossbow. NoSpell for none.
    SpellId ShootSpell(RangedKind kind);

    // The bot can fire the weapon: it wears one and has ammo for it.
    bool CanFire(RangedWeapon const& weapon);
}

#endif
