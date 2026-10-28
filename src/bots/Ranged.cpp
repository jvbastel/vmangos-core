#include "Ranged.h"
#include "BotView.h"
#include "BotGeometry.h"

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
            case RangedKind::Wand: return WandShoot;
            case RangedKind::None: break;
        }
        return NoSpell;
    }

    bool CanFire(RangedWeapon const& weapon)
    {
        if (weapon.kind == RangedKind::Wand)
            return true;
        return weapon.kind != RangedKind::None && weapon.ammoEntry != 0 && weapon.ammoCount > 0;
    }

    bool CanShoot(BotView const& view)
    {
        RangedWeapon const weapon = view.GetRangedWeapon();
        return CanFire(weapon) && view.KnowsSpell(ShootSpell(weapon.kind));
    }

    ShotReach ShotReachAt(float spellYards)
    {
        if (spellYards < ShotMinYards)
            return ShotReach::TooNear;
        return spellYards > ShotMaxYards ? ShotReach::TooFar : ShotReach::InRange;
    }

    bool InShotFrom(BotView const& view, Position const& at, Guid target, float minYards)
    {
        float const yards = view.GetSpellDistanceFrom(at, target);
        return yards >= minYards && yards <= ShotMaxYards && view.CanSeeFrom(at, target);
    }

    bool InWandShot(BotView const& view, Guid target)
    {
        return InShotFrom(view, view.GetPosition(view.GetSelf()), target, 0.0f);
    }

    bool FindShotOnPath(BotView const& view, Guid target, Position& spot)
    {
        std::vector<Position> path;
        if (!view.FindGroundPath(view.GetPosition(view.GetSelf()), view.GetPosition(target), path))
            return false;
        return FindPointOnPath(path, ShotPathStepYards,
            [&](Position const& step) { return InShotFrom(view, step, target); }, spot);
    }
}
