#include "BotGeometry.h"
#include "ClassModules.h"
#include "FightSpot.h"
#include "FakeBot.h"
#include "WarriorModule.h"

#include <gtest/gtest.h>

using Bots::FightSpot;
using Bots::FindFightSpot;
using Bots::Position;
using Bots::Tests::FakeBot;

// The caster pack of jvbastel/WOWAI#121, at pack9 of Ragefire Chasm: one
// Ragefire Shaman that stays at its spawn and casts. pack10 and pack11 stand
// 16.8 and 13.9 yd from it. The positions are those of the route file.
namespace
{
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Shaman = 0xF130002C0B00BCBE;   // pack9
    Bots::Guid const Shaman10 = 0xF130002C0B00BCB1; // pack10
    Bots::Guid const Trogg10 = 0xF130002C0A00BCB4;  // pack10
    Bots::Guid const Trogg11 = 0xF130002C0A00BCB2;  // pack11

    Position const P17 = {-205.46f, -31.92f, -49.76f};
    Position const PullPack8 = {-214.01f, -33.16f, -52.72f};
    Position const PullPack9 = {-225.85f, -32.92f, -55.42f};
    Position const Pack9 = {-253.89f, -38.88f, -60.46f};
    Position const Pack10 = {-266.73f, -28.03f, -60.61f};
    Position const Pack11 = {-266.61f, -44.40f, -60.85f};
    float const AggroYards = 15.0f;

    // The way of the full run, p17 to pull_pack8 to pull_pack9, a point
    // each 2 yards, oldest first.
    std::vector<Position> Way()
    {
        std::vector<Position> way;
        Position const corners[] = {P17, PullPack8, PullPack9};
        for (int leg = 0; leg < 2; ++leg)
        {
            Position const a = corners[leg], b = corners[leg + 1];
            int const steps = int(Bots::Distance3d(a, b) / 2.0f);
            for (int i = 0; i < steps; ++i)
            {
                float const t = float(i) / float(steps);
                way.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
            }
        }
        way.push_back(PullPack9);
        return way;
    }

    // The tank at pull_pack9 with the way behind it. A rock at x = -219
    // hides the way from p17 to near pull_pack8 from the shaman's spawn
    // (fake line of sight); pull_pack9 sees the shaman.
    struct CasterPull
    {
        FakeBot fake{Tank};
        std::vector<Position> trail = Way();

        CasterPull()
        {
            fake.SetPosition(Tank, PullPack9);
            AddEnemy(Shaman, Pack9);
            AddEnemy(Shaman10, Pack10);
            AddEnemy(Trogg10, {Pack10.x - 2.0f, Pack10.y + 2.0f, Pack10.z});
            AddEnemy(Trogg11, Pack11);
            fake.SetMana(Shaman, 400, 400);
            fake.SetMana(Shaman10, 400, 400);
            fake.AddSightWall({-219.0f, -40.0f, 0.0f}, {-219.0f, -31.0f, 0.0f});
        }

        void AddEnemy(Bots::Guid enemy, Position at)
        {
            fake.SetPosition(enemy, at);
            fake.AddEnemy(enemy, at, AggroYards);
        }

        FightSpot Find() const { return FindFightSpot(fake, trail, Shaman); }
    };
}

TEST(CasterPack, TheTankFightsACasterPackWhereTheCasterCannotSeeItsSpawn)
{
    CasterPull pull;

    FightSpot const spot = pull.Find();

    EXPECT_TRUE(spot.clean);
    EXPECT_TRUE(spot.lineOfSight);
    EXPECT_FALSE(pull.fake.CanSeeBetween(Pack9, spot.spot));
    // The first such spot back on the way, near enough to the shot from
    // where the tank stands.
    EXPECT_LE(Bots::Distance2d(spot.spot, PullPack9), 10.0f);
    EXPECT_FALSE(spot.hasShootSpot);
}

TEST(CasterPack, APackWithoutManaIsNoCasterPack)
{
    CasterPull pull;
    pull.fake.SetMana(Shaman, 0, 0);

    FightSpot const spot = pull.Find();

    EXPECT_TRUE(spot.clean);
    EXPECT_FALSE(spot.lineOfSight);
    EXPECT_EQ(spot.spot.x, PullPack9.x);
}

namespace
{
    namespace Warrior = Bots::Warrior;

    // The tank of the group at pull_pack9, with a bow, that walked the way
    // of the full run (its trail).
    struct CasterTank : CasterPull
    {
        Bots::GroupCoordinator group;
        Bots::StrategyStack stack = Bots::MakeStrategyStack(Bots::BotClass::Warrior, Bots::BotRole::Tank);
        std::int64_t nowMs = 1000;

        explicit CasterTank(bool withWay = true)
        {
            for (Bots::SpellId spell : {Warrior::BattleStance, Warrior::DefensiveStance, Warrior::Taunt,
                     Warrior::HeroicStrike, Warrior::SunderArmor, Warrior::Revenge, Warrior::BattleShout,
                     Warrior::Bloodrage, Bots::ShootSpell(Bots::RangedKind::Bow)})
                fake.SetKnownSpell(spell);
            Bots::RangedWeapon bow;
            bow.kind = Bots::RangedKind::Bow;
            bow.entry = 2504;
            bow.ammoEntry = 2512;
            bow.ammoCount = 200;
            fake.SetRangedWeapon(bow);
            fake.SetHealth(Tank, 600, 600);
            fake.SetPower(Tank, 0, 100);
            fake.SetAura(Tank, Warrior::DefensiveStance);
            fake.SetAura(Tank, Warrior::BattleShout);
            if (!withWay)
                trail = {PullPack9};
            for (Position const& point : trail)
                group.KeepTrail(point, false);
        }

        std::string Think()
        {
            Bots::BotContext context{fake, fake, group, Bots::CommandState::None, Bots::NoGuid};
            context.nowMs = nowMs;
            return stack.Think(context);
        }

        // The pull of the shaman: the tank notes its fight spot, the shot
        // hits, the shaman attacks the tank, and the tank is back at its
        // fight spot. The shaman stays at its spawn and casts.
        void PullAndHold()
        {
            group.StartPull(Tank, Shaman);
            Think();
            ASSERT_TRUE(group.GetPull().hasFightSpot);
            fake.SetPosition(Tank, group.GetPull().fightSpot);
            fake.SetInCombat(Tank);
            fake.SetInCombat(Shaman);
            fake.SetVictim(Shaman, Tank);
            // In a fight the tank's trail is empty (Trail.h).
            group.KeepTrail(group.GetPull().fightSpot, true);
            Think(); // Holds the pull.
            Think(); // Back at the fight spot.
            ASSERT_EQ(group.GetPull().phase, Bots::PullPhase::Holding);
            ASSERT_FALSE(group.GetPull().hasFightSpot);
            fake.ClearDeeds();
        }

        bool Chases() const
        {
            for (std::string const& deed : fake.Deeds())
                if (deed.compare(0, 5, "chase") == 0)
                    return true;
            return false;
        }
    };
}

// The shaman stays at its spawn, where pack10 and pack11 would join a fight:
// the tank does not run to it.
TEST(CasterPack, TheTankDoesNotRunToACasterThatStaysNearOtherPacks)
{
    CasterTank tank;
    tank.PullAndHold();

    tank.Think();
    tank.nowMs += 1000;
    tank.Think();

    EXPECT_FALSE(tank.Chases());
    EXPECT_FALSE(tank.group.GetPull().hasFightSpot);
}

// User note on jvbastel/WOWAI#121: when the caster does not follow at once,
// the tank runs a bit further out along its cleared way, where the caster
// cannot see it.
TEST(CasterPack, ACasterThatDoesNotComeMakesTheTankRunFurtherOut)
{
    CasterTank tank;
    tank.PullAndHold();
    Position const heldAt = tank.fake.GetPosition(Tank);
    tank.Think();

    tank.nowMs += Bots::FocusComeMs;
    tank.Think();
    tank.Think();

    Bots::PullState const pull = tank.group.GetPull();
    ASSERT_TRUE(pull.hasFightSpot);
    EXPECT_GE(Bots::Distance3d(pull.fightSpot, heldAt), Bots::RunOutStepYards);
    EXPECT_GT(Bots::Distance3d(pull.fightSpot, Pack9), Bots::Distance3d(heldAt, Pack9));
    EXPECT_FALSE(tank.fake.CanSeeBetween(Pack9, pull.fightSpot));
    EXPECT_FALSE(tank.Chases());
    ASSERT_FALSE(tank.fake.Deeds().empty());
    EXPECT_EQ(tank.fake.Deeds().back().compare(0, 12, "keep move to"), 0);
}

// The shaman came round the rock, far enough from the other packs: the
// tank fights it there.
TEST(CasterPack, TheTankFightsTheCasterOnceItStandsFarFromTheOtherPacks)
{
    CasterTank tank;
    tank.PullAndHold();
    tank.Think();
    tank.fake.SetPosition(Shaman, {-229.0f, -33.0f, -55.5f});

    tank.Think();

    EXPECT_TRUE(tank.Chases());
}

// Plan item 4 of jvbastel/WOWAI#121: the tank warns the moment another pack
// joins the fight, once a pack, whatever brought it.
TEST(CasterPack, TheTankWarnsTheMomentAnotherPackJoinsOnceAPack)
{
    CasterTank tank;
    tank.PullAndHold();
    std::string const warnOne = "warn Careful: this pull brings 1 more pack.";
    auto warns = [&tank, &warnOne]() {
        return std::count(tank.fake.Deeds().begin(), tank.fake.Deeds().end(), warnOne);
    };

    tank.fake.SetInCombat(Trogg11);
    tank.fake.SetVictim(Trogg11, Tank);
    tank.Think();
    tank.Think();
    EXPECT_EQ(warns(), 1);

    tank.fake.SetInCombat(Shaman10);
    tank.fake.SetVictim(Shaman10, Tank);
    tank.Think();
    tank.fake.SetInCombat(Trogg10);
    tank.fake.SetVictim(Trogg10, Tank);
    tank.Think();
    tank.Think();
    EXPECT_EQ(warns(), 2);
}

// No cleared way is left behind the tank: the fight goes to the caster.
// The tank warns first, then goes in.
TEST(CasterPack, WithNoWayLeftTheTankWarnsAndGoesIn)
{
    CasterTank tank(false);
    tank.PullAndHold();
    tank.Think();
    tank.nowMs += Bots::FocusComeMs;

    tank.Think();
    std::vector<std::string> const warned = tank.fake.Deeds();
    tank.fake.ClearDeeds();
    tank.Think();

    EXPECT_EQ(warned, (std::vector<std::string>{"warn Careful: this pull brings 2 more packs."}));
    EXPECT_TRUE(tank.Chases());
}
