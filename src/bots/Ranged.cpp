#include "Ranged.h"
#include "BotView.h"
#include "BotGeometry.h"

#include <algorithm>
#include <vector>

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

    ShotReach ShotReachAt(float spellYards)
    {
        if (spellYards < ShotMinYards)
            return ShotReach::TooNear;
        return spellYards > ShotMaxYards ? ShotReach::TooFar : ShotReach::InRange;
    }

    bool InShotFrom(BotView const& view, Position const& at, Guid target)
    {
        return ShotReachAt(view.GetSpellDistanceFrom(at, target)) == ShotReach::InRange && view.CanSeeFrom(at, target);
    }

    bool FindShotOnPath(BotView const& view, Guid target, Position& spot)
    {
        std::vector<Position> path;
        if (!view.FindGroundPath(view.GetPosition(view.GetSelf()), view.GetPosition(target), path))
            return false;
        for (std::size_t i = 1; i < path.size(); ++i)
        {
            Position const& a = path[i - 1];
            Position const& b = path[i];
            int const steps = std::max(1, int(Distance2d(a, b) / ShotPathStepYards));
            for (int k = 0; k <= steps; ++k)
            {
                float const t = float(k) / float(steps);
                Position const step{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
                if (InShotFrom(view, step, target))
                {
                    spot = step;
                    return true;
                }
            }
        }
        return false;
    }
}
