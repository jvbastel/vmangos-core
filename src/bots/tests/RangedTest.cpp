#include "Ranged.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

using Bots::AmmoTopUp;
using Bots::PlanAmmoTopUp;
using Bots::RangedKind;
using Bots::RangedWeapon;

namespace
{
    std::uint32_t const WornShortbow = 2504;
    std::uint32_t const OldBlunderbuss = 2508;
    std::uint32_t const LightCrossbow = 15807;
    std::uint32_t const SmallThrowingKnife = 2947;
    std::uint32_t const RoughArrow = 2512;
    std::uint32_t const SharpArrow = 2515;
    std::uint32_t const RazorArrow = 3030;
    std::uint32_t const JaggedArrow = 11285;
    std::uint32_t const LightShot = 2516;
    std::uint32_t const HeavyShot = 2519;
    std::uint32_t const AccurateSlugs = 11284;

    RangedWeapon Weapon(RangedKind kind, std::uint32_t entry, std::uint32_t ammoEntry, std::uint32_t ammoCount)
    {
        RangedWeapon weapon;
        weapon.kind = kind;
        weapon.entry = entry;
        weapon.ammoEntry = ammoEntry;
        weapon.ammoCount = ammoCount;
        return weapon;
    }

    std::pair<std::uint32_t, std::uint32_t> Pair(AmmoTopUp const& topUp)
    {
        return {topUp.entry, topUp.count};
    }
}

TEST(Ammo, ABowGetsTheArrowsOfTheLevel)
{
    EXPECT_EQ(RoughArrow, Bots::AmmoFor(RangedKind::Bow, 1));
    EXPECT_EQ(RoughArrow, Bots::AmmoFor(RangedKind::Bow, 9));
    EXPECT_EQ(SharpArrow, Bots::AmmoFor(RangedKind::Bow, 10));
    EXPECT_EQ(RazorArrow, Bots::AmmoFor(RangedKind::Bow, 30));
    EXPECT_EQ(JaggedArrow, Bots::AmmoFor(RangedKind::Bow, 60));
}

TEST(Ammo, ACrossbowShootsArrowsAndAGunBullets)
{
    EXPECT_EQ(SharpArrow, Bots::AmmoFor(RangedKind::Crossbow, 12));
    EXPECT_EQ(LightShot, Bots::AmmoFor(RangedKind::Gun, 5));
    EXPECT_EQ(HeavyShot, Bots::AmmoFor(RangedKind::Gun, 12));
    EXPECT_EQ(AccurateSlugs, Bots::AmmoFor(RangedKind::Gun, 40));
    EXPECT_EQ(0u, Bots::AmmoFor(RangedKind::Thrown, 12));
}

TEST(Ammo, ABowWithFewArrowsIsToppedUpWithoutBuying)
{
    AmmoTopUp const topUp = PlanAmmoTopUp(Weapon(RangedKind::Bow, WornShortbow, RazorArrow, 20), 30);

    EXPECT_EQ(Pair(topUp), std::make_pair(RazorArrow, 200u));
}

TEST(Ammo, AGunWithNoBulletsGetsTheBulletsOfItsLevel)
{
    AmmoTopUp const topUp = PlanAmmoTopUp(Weapon(RangedKind::Gun, OldBlunderbuss, 0, 0), 12);

    EXPECT_EQ(Pair(topUp), std::make_pair(HeavyShot, 200u));
}

TEST(Ammo, EnoughAmmoOfTheLevelNeedsNoTopUp)
{
    EXPECT_EQ(0u, PlanAmmoTopUp(Weapon(RangedKind::Crossbow, LightCrossbow, SharpArrow, 150), 12).count);
}

TEST(Ammo, ALevelUpSwitchesToTheBetterAmmo)
{
    AmmoTopUp const topUp = PlanAmmoTopUp(Weapon(RangedKind::Bow, WornShortbow, SharpArrow, 200), 25);

    EXPECT_EQ(Pair(topUp), std::make_pair(RazorArrow, 200u));
}

TEST(Ammo, AThrownWeaponTopsUpItsOwnStack)
{
    RangedWeapon const knives = Weapon(RangedKind::Thrown, SmallThrowingKnife, SmallThrowingKnife, 30);

    EXPECT_EQ(Pair(PlanAmmoTopUp(knives, 5)), std::make_pair(SmallThrowingKnife, 200u));
    EXPECT_EQ(0u, PlanAmmoTopUp(Weapon(RangedKind::Thrown, SmallThrowingKnife, SmallThrowingKnife, 150), 5).count);
}

TEST(Ammo, NoRangedWeaponNeedsNoAmmo)
{
    EXPECT_EQ(0u, PlanAmmoTopUp(RangedWeapon{}, 30).count);
}

TEST(Ammo, TheShootSpellFitsTheWeapon)
{
    EXPECT_EQ(2764u, Bots::ShootSpell(RangedKind::Thrown));
    EXPECT_EQ(2480u, Bots::ShootSpell(RangedKind::Bow));
    EXPECT_EQ(7918u, Bots::ShootSpell(RangedKind::Gun));
    EXPECT_EQ(7919u, Bots::ShootSpell(RangedKind::Crossbow));
    EXPECT_EQ(Bots::NoSpell, Bots::ShootSpell(RangedKind::None));
}

TEST(Ammo, ABotFiresOnlyWithAWeaponAndAmmo)
{
    EXPECT_TRUE(Bots::CanFire(Weapon(RangedKind::Bow, WornShortbow, RoughArrow, 1)));
    EXPECT_FALSE(Bots::CanFire(Weapon(RangedKind::Bow, WornShortbow, RoughArrow, 0)));
    EXPECT_FALSE(Bots::CanFire(Weapon(RangedKind::Bow, WornShortbow, 0, 0)));
    EXPECT_TRUE(Bots::CanFire(Weapon(RangedKind::Thrown, SmallThrowingKnife, SmallThrowingKnife, 1)));
    EXPECT_FALSE(Bots::CanFire(RangedWeapon{}));
}

// The one "in shot" rule (jvbastel/WOWAI#100 review): 8 to 30 yards as spell
// ranges measure them, and in sight.
TEST(Shot, ReachesFrom8To30YardsOfSpellDistance)
{
    EXPECT_EQ(Bots::ShotReachAt(7.9f), Bots::ShotReach::TooNear);
    EXPECT_EQ(Bots::ShotReachAt(8.0f), Bots::ShotReach::InRange);
    EXPECT_EQ(Bots::ShotReachAt(30.0f), Bots::ShotReach::InRange);
    EXPECT_EQ(Bots::ShotReachAt(30.1f), Bots::ShotReach::TooFar);
}

TEST(Shot, FromASpotNeedsRangeAndSight)
{
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Kobold = 0xF130000302000022;
    Bots::Tests::FakeBot fake{Tank};
    fake.SetPosition(Tank, {0, 0, 0});
    fake.SetPosition(Kobold, {20, 0, 0});
    fake.AddBlindToFromX(Kobold, 5.0f);

    EXPECT_TRUE(Bots::InShotFrom(fake, {0, 0, 0}, Kobold));
    EXPECT_FALSE(Bots::InShotFrom(fake, {6, 0, 0}, Kobold));   // Out of sight.
    EXPECT_FALSE(Bots::InShotFrom(fake, {-15, 0, 0}, Kobold)); // 35 yards.
    EXPECT_FALSE(Bots::InShotFrom(fake, {14, 0, -100}, Kobold)); // Far under it.
}
