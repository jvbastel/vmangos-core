#include "Ranged.h"

namespace Bots
{
    namespace
    {
        // Vendor ammo by required level, highest first.
        struct AmmoItem
        {
            std::uint32_t level;
            std::uint32_t arrow;
            std::uint32_t bullet;
        };
        AmmoItem const AmmoItems[] = {
            {40, 11285, 11284}, // Jagged Arrow, Accurate Slugs
            {25, 3030, 3033},   // Razor Arrow, Solid Shot
            {10, 2515, 2519},   // Sharp Arrow, Heavy Shot
            {1, 2512, 2516},    // Rough Arrow, Light Shot
        };
    }

    std::uint32_t AmmoFor(RangedKind kind, std::uint32_t level)
    {
        if (kind != RangedKind::Bow && kind != RangedKind::Crossbow && kind != RangedKind::Gun)
            return 0;
        for (AmmoItem const& item : AmmoItems)
            if (level >= item.level)
                return kind == RangedKind::Gun ? item.bullet : item.arrow;
        return 0;
    }

    AmmoTopUp PlanAmmoTopUp(RangedWeapon const& weapon, std::uint32_t level)
    {
        std::uint32_t const best = weapon.kind == RangedKind::Thrown ? weapon.entry : AmmoFor(weapon.kind, level);
        if (best == 0 || (weapon.ammoEntry == best && weapon.ammoCount >= LowAmmo))
            return {};
        return {best, AmmoStack};
    }

    SpellId ShootSpell(RangedKind kind)
    {
        switch (kind)
        {
            case RangedKind::Thrown: return 2764;   // Throw
            case RangedKind::Bow: return 2480;      // Shoot Bow
            case RangedKind::Gun: return 7918;      // Shoot Gun
            case RangedKind::Crossbow: return 7919; // Shoot Crossbow
            case RangedKind::None: break;
        }
        return NoSpell;
    }

    bool CanFire(RangedWeapon const& weapon)
    {
        return weapon.kind != RangedKind::None && weapon.ammoEntry != 0 && weapon.ammoCount > 0;
    }
}
