#include "BotGeometry.h"
#include "ClassModules.h"
#include "DamageRules.h"
#include "FightSpot.h"
#include "FakeBot.h"
#include "GroupCoordinator.h"
#include "KeepOut.h"
#include "SafePlace.h"
#include "WarriorModule.h"

#include <algorithm>
#include <cstdio>
#include <gtest/gtest.h>

using Bots::FightSpot;
using Bots::FindFightSpot;
using Bots::Position;
using Bots::Tests::FakeBot;

// The caster pack of jvbastel/WOWAI#121, at pack9 of Ragefire Chasm: one
// Ragefire Shaman that stays at its spawn and casts. pack10 and pack11
// stand 16.8 and 13.9 yd from it. The positions are those of the route file
// (bots/data/routes/ragefire_chasm.txt). The line of sight is fake: a rock
// at x = -219 hides the way from p17 to near pull_pack8 from the shaman.
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

    // The tank at pull_pack9 with the way behind it, pack9 to pack11 in
    // place, and the rock.
    struct CasterPull
    {
        FakeBot fake{Tank};
        std::vector<Position> trail = Way();

        // `rockX`: the x of the rock, or 0 for none.
        explicit CasterPull(float rockX = -219.0f)
        {
            fake.SetPosition(Tank, PullPack9);
            AddEnemy(Shaman, Pack9);
            AddEnemy(Shaman10, Pack10);
            AddEnemy(Trogg10, {Pack10.x - 2.0f, Pack10.y + 2.0f, Pack10.z});
            AddEnemy(Trogg11, Pack11);
            fake.SetMana(Shaman, 400, 400);
            fake.SetMana(Shaman10, 400, 400);
            if (rockX != 0.0f)
                fake.AddSightWall({rockX, -40.0f, 0.0f}, {rockX, -31.0f, 0.0f});
        }

        void AddEnemy(Bots::Guid enemy, Position at)
        {
            fake.SetPosition(enemy, at);
            fake.AddEnemy(enemy, at, AggroYards);
        }

        FightSpot Find() const { return FindFightSpot(fake, trail, Shaman); }
    };
}

// Plan item 2: the line-of-sight pull. The first spot back on the way that
// the shaman cannot see from its spawn, with the shot from pull_pack9
// (within 10 yd of it).
TEST(CasterPack, TheTankFightsACasterPackWhereTheCasterCannotSeeIt)
{
    CasterPull pull;

    FightSpot const spot = pull.Find();

    EXPECT_TRUE(spot.clean);
    EXPECT_TRUE(spot.lineOfSight);
    EXPECT_GT(spot.spot.x, -219.0f);
    EXPECT_LE(Bots::Distance2d(spot.spot, PullPack9), 10.0f);
    EXPECT_FALSE(spot.hasShootSpot);
}

// Plan item 1: a pack with no mana is no caster pack, and the tank fights
// where it stands, as before.
TEST(CasterPack, APackWithoutManaIsNoCasterPack)
{
    CasterPull pull;
    pull.fake.SetMana(Shaman, 0, 0);

    FightSpot const spot = pull.Find();

    EXPECT_TRUE(spot.clean);
    EXPECT_FALSE(spot.lineOfSight);
    EXPECT_EQ(spot.spot.x, PullPack9.x);
}

// Plan item 3: with no spot out of the caster's sight, the search judges
// the area round its spawn: pack11 (13.9 yd) and pack10 (16.8 yd) are in
// the aggro distance and margin there, so the pull warns of 2 packs.
TEST(CasterPack, WithNoSpotOutOfSightTheSpawnAreaCountsAndThePullWarns)
{
    CasterPull pull(0.0f);

    FightSpot const spot = pull.Find();

    EXPECT_FALSE(spot.lineOfSight);
    EXPECT_FALSE(spot.clean);
    EXPECT_EQ(spot.extraPacks, 2u);
}

// A shot within 10 yd of the spot is needed: a spot out of sight with the
// shot further away does not count.
TEST(CasterPack, ASpotOutOfSightNeedsAShotWithin10Yards)
{
    // The rock hides only p17 and the way next to it, 13 yd and more from
    // pull_pack9, where the tank shoots.
    CasterPull pull(-211.0f);
    // The shaman is out of shot from everywhere but pull_pack9.
    pull.fake.AddBlindToFromX(Shaman, -224.0f);

    FightSpot const spot = pull.Find();

    EXPECT_FALSE(spot.lineOfSight);
    EXPECT_FALSE(spot.clean);
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

        explicit CasterTank(float rockX = -219.0f) : CasterPull(rockX)
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
            fake.SetPower(Tank, 50, 100);
            fake.SetAura(Tank, Warrior::DefensiveStance);
            fake.SetAura(Tank, Warrior::BattleShout);
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
            return std::any_of(fake.Deeds().begin(), fake.Deeds().end(),
                [](std::string const& deed) { return deed.compare(0, 5, "chase") == 0; });
        }

        long Warns() const
        {
            return std::count_if(fake.Deeds().begin(), fake.Deeds().end(),
                [](std::string const& deed) { return deed.compare(0, 5, "warn ") == 0; });
        }
    };
}

// Plan item 3: the shaman stays at its spawn. For CasterComeMs the tank
// waits for it at its fight spot, and does not run to it.
TEST(CasterPack, TheTankWaitsForACasterThatStaysAway)
{
    CasterTank tank;
    tank.PullAndHold();

    tank.nowMs += Bots::CasterComeMs - 1000;
    tank.Think();
    tank.Think();

    EXPECT_FALSE(tank.Chases());
}

// Plan item 3: the shaman came round the rock, where no other pack is
// near, and stays there: after CasterComeMs the tank goes in, with no warn.
TEST(CasterPack, ACasterThatStaysInACleanAreaGetsTheTankWithNoWarn)
{
    CasterTank tank;
    tank.PullAndHold();
    tank.fake.SetPosition(Shaman, {-236.0f, -34.0f, -56.5f});

    tank.nowMs += Bots::CasterComeMs;
    tank.Think();
    tank.Think();

    EXPECT_TRUE(tank.Chases());
    EXPECT_EQ(tank.Warns(), 0);
    EXPECT_TRUE(tank.group.GetPull().goesIn);
}

// Plan item 3: the shaman stays at its spawn, in reach of pack10 and pack11:
// after CasterComeMs the tank warns first, then goes in.
TEST(CasterPack, ACasterThatStaysNearOtherPacksGetsTheTankAfterAWarn)
{
    CasterTank tank;
    tank.PullAndHold();

    tank.nowMs += Bots::CasterComeMs;
    tank.Think();
    std::vector<std::string> const first = tank.fake.Deeds();
    tank.fake.ClearDeeds();
    tank.Think();

    EXPECT_EQ(first, (std::vector<std::string>{"warn Careful: this pull brings 2 more packs."}));
    EXPECT_TRUE(tank.Chases());
}

// The pull warned already (no line-of-sight spot): the tank goes in with no
// second warn.
TEST(CasterPack, APullThatWarnedBeforeGoesInWithNoSecondWarn)
{
    CasterTank tank(0.0f);
    tank.group.StartPull(Tank, Shaman);
    tank.Think();
    EXPECT_EQ(tank.Warns(), 1);
    tank.fake.ClearDeeds();
    tank.fake.SetPosition(Tank, tank.group.GetPull().fightSpot);
    tank.fake.SetInCombat(Tank);
    tank.fake.SetInCombat(Shaman);
    tank.fake.SetVictim(Shaman, Tank);
    tank.Think();
    tank.Think();

    tank.nowMs += Bots::CasterComeMs;
    tank.Think();
    tank.Think();

    EXPECT_EQ(tank.Warns(), 0);
    EXPECT_TRUE(tank.Chases());
}

// Plan item 4: the tank warns the moment another pack joins the fight,
// once a pack, whatever brought it.
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

namespace
{
    // The place of a "move to x y z" deed (FakeBot), or false.
    bool MovesTo(std::vector<std::string> const& deeds, Position& to)
    {
        for (std::string const& deed : deeds)
            if (std::sscanf(deed.c_str(), "move to %f %f %f", &to.x, &to.y, &to.z) == 3)
                return true;
        return false;
    }

    // Near the shaman's spawn: 17.2 yd from pack11, in its aggro distance
    // and margin (20 yd).
    Position const NearPack11 = {-250.0f, -40.0f, -60.0f};
}

// Plan item 5: the tank has not gone in, and stands within the safe place
// distance of pack11: it steps out first.
TEST(CasterPack, TheTankStepsOutOfThePlaceOfAPackOutsideTheFight)
{
    CasterTank tank;
    tank.PullAndHold();
    tank.fake.SetPosition(Tank, NearPack11);

    tank.Think();

    Position to{};
    ASSERT_TRUE(MovesTo(tank.fake.Deeds(), to));
    for (Position const& pack : {Pack10, Pack11})
        EXPECT_GT(Bots::Distance3d(to, pack), AggroYards + Bots::AggroMarginYards);
}

// Plan item 5, the one exception: the tank goes in after its warn.
TEST(CasterPack, TheTankThatGoesInAfterAWarnMayStepNearOtherPacks)
{
    CasterTank tank;
    tank.PullAndHold();
    tank.nowMs += Bots::CasterComeMs;
    tank.Think(); // Warns, and goes in.
    tank.fake.SetPosition(Tank, NearPack11);
    tank.fake.ClearDeeds();

    tank.Think();

    Position to{};
    EXPECT_FALSE(MovesTo(tank.fake.Deeds(), to));
    EXPECT_TRUE(tank.Chases());
}

// Plan item 5: a damage bot in the fight of the pull steps out of the place
// of pack11 too.
TEST(CasterPack, AMemberStepsOutOfThePlaceOfAPackOutsideTheFight)
{
    Bots::Guid const Rogue = 0x13;
    CasterPull pull;
    FakeBot& fake = pull.fake;
    fake = FakeBot{Rogue};
    pull.AddEnemy(Shaman, Pack9);
    pull.AddEnemy(Shaman10, Pack10);
    pull.AddEnemy(Trogg11, Pack11);
    fake.SetMana(Shaman, 400, 400);
    fake.SetHealth(Rogue, 500, 500);
    fake.SetHealth(Tank, 600, 600);
    fake.SetGroupMembers({Tank});
    fake.SetPosition(Tank, PullPack9);
    fake.SetPosition(Rogue, NearPack11);
    fake.SetInCombat(Rogue);
    fake.SetVictim(Shaman, Tank);
    Bots::GroupCoordinator group;
    group.SetTank(Tank);
    group.StartPull(Tank, Shaman);
    group.HoldPull(Tank, 1000);
    Bots::StrategyStack const stack = Bots::MakeStrategyStack(Bots::BotClass::Rogue, Bots::BotRole::Melee);
    Bots::BotContext context{fake, fake, group, Bots::CommandState::None, Bots::NoGuid};

    EXPECT_EQ(stack.Think(context), Bots::KeepOutRule);

    Position to{};
    ASSERT_TRUE(MovesTo(fake.Deeds(), to));
    EXPECT_GT(Bots::Distance3d(to, Pack11), AggroYards + Bots::AggroMarginYards);
}

// Out of a pull and out of combat, the group walks the way: no keep-out.
TEST(CasterPack, NoKeepOutOutOfAFight)
{
    Bots::Guid const Rogue = 0x13;
    FakeBot fake{Rogue};
    fake.SetHealth(Rogue, 500, 500);
    fake.SetPosition(Rogue, NearPack11);
    fake.SetPosition(Trogg11, Pack11);
    fake.AddEnemy(Trogg11, Pack11, AggroYards);
    Bots::GroupCoordinator group;
    Bots::StrategyStack const stack = Bots::MakeStrategyStack(Bots::BotClass::Rogue, Bots::BotRole::Melee);
    Bots::BotContext context{fake, fake, group, Bots::CommandState::None, Bots::NoGuid};

    EXPECT_NE(stack.Think(context), Bots::KeepOutRule);
}

// Plan item 5, merge review: with no safe place to step out to, the
// keep-out rule leaves the think to the other rules: the tank keeps its
// fight and does not freeze.
TEST(CasterPack, WithNoSafePlaceTheKeepOutRuleDoesNotFreezeTheTank)
{
    CasterTank tank;
    tank.PullAndHold();
    tank.fake.SetPosition(Tank, NearPack11);
    tank.fake.AddNoPathFromX(-10000.0f);

    EXPECT_NE(tank.Think(), Bots::KeepOutRule);
}

// Review of jvbastel/WOWAI#121 (C2): the keep-out rule steps a melee damage
// bot out, and its melee rule chased its target back in: a loop. As for the
// tank's pick-up, the melee bot does not chase a target in the reach of a
// pack outside the fight. The member that the enemy hits steps out itself.
TEST(CasterPack, AMeleeDamageBotDoesNotChaseATargetInThePlaceOfAPackOutsideTheFight)
{
    Bots::Guid const Rogue = 0x13;
    Bots::Guid const Healer = 0x12;
    FakeBot fake{Rogue};
    fake.SetHealth(Rogue, 500, 500);
    fake.SetHealth(Tank, 600, 600);
    fake.SetHealth(Healer, 400, 400);
    fake.SetGroupMembers({Tank, Healer});
    fake.SetPosition(Tank, PullPack9);
    fake.SetPosition(Rogue, PullPack9);
    fake.SetPosition(Healer, NearPack11);
    fake.SetInCombat(Rogue);
    fake.SetPosition(Trogg11, Pack11);
    fake.AddEnemy(Trogg11, Pack11, AggroYards);
    // The add hits the healer near pack11.
    fake.SetPosition(Trogg10, NearPack11);
    fake.AddEnemy(Trogg10, NearPack11, AggroYards);
    fake.SetHealth(Trogg10, 300, 300);
    fake.SetVictim(Trogg10, Healer);
    fake.SetDistanceBetween(Trogg10, Healer, 1.0f);
    Bots::GroupCoordinator group;
    group.SetTank(Tank);
    group.SetFocusTarget(Shaman);
    fake.SetPosition(Shaman, Pack9);
    fake.SetVictim(Shaman, Tank);
    Bots::StrategyStack const stack = Bots::MakeStrategyStack(Bots::BotClass::Rogue, Bots::BotRole::Melee);
    Bots::BotContext context{fake, fake, group, Bots::CommandState::None, Bots::NoGuid};

    stack.Think(context);

    for (std::string const& deed : fake.Deeds())
        EXPECT_EQ(deed.find("chase"), std::string::npos) << deed;
}

// Test 1 at 09:44 on 2026-10-29: the player tank fought the focus in the
// reach of the next pack, and the rogue stood idle the whole fight. The tank
// holds the focus there in melee contact and stands in that reach itself:
// the melee bot hits the focus, and the keep-out rule does not step it out.
namespace
{
    struct TankInOtherPackReach
    {
        Bots::Guid const Rogue = 0x13;
        FakeBot fake{Rogue};
        Bots::GroupCoordinator group;

        // `tankAt`: where the tank stands, in melee contact with the shaman.
        explicit TankInOtherPackReach(Position tankAt)
        {
            fake.SetHealth(Rogue, 500, 500);
            fake.SetHealth(Tank, 600, 600);
            fake.SetGroupMembers({Tank});
            fake.SetInCombat(Rogue);
            fake.SetPosition(Trogg11, Pack11);
            fake.AddEnemy(Trogg11, Pack11, AggroYards);
            fake.SetPosition(Shaman, Pack9);
            fake.SetHealth(Shaman, 300, 300);
            fake.SetPosition(Tank, tankAt);
            fake.SetVictim(Shaman, Tank);
            fake.SetDistanceBetween(Shaman, Tank, 1.0f);
            // At pack11's reach, as in the step-out test above: the
            // contact with the shaman is the fake's.
            fake.SetPosition(Rogue, NearPack11);
            group.SetTank(Tank);
            group.StartPull(Tank, Shaman);
            group.HoldPull(Tank, 0);
            group.SetFocusTarget(Shaman);
            // As the bot AI sets it on each think.
            group.SetMemberRole(Rogue, Bots::BotRole::Melee);
        }

        // The tank holds threat: contact at 0 ms, held at HoldContactMs.
        Bots::BotContext Context()
        {
            Bots::BotContext context{fake, fake, group, Bots::CommandState::None, Bots::NoGuid};
            Bots::PickMeleeDamageTarget(context);
            context.nowMs = Bots::HoldContactMs;
            return context;
        }
    };

    Position const AtTheShaman = {Pack9.x + 1.0f, Pack9.y, Pack9.z};
}

TEST(CasterPack, AMeleeBotHitsTheFocusThatTheTankHoldsInTheReachOfAnotherPack)
{
    TankInOtherPackReach fight(AtTheShaman);
    Bots::BotContext const context = fight.Context();
    ASSERT_TRUE(Bots::InOtherPacksReach(context, Pack9));
    ASSERT_TRUE(Bots::InOtherPacksReach(context, AtTheShaman));

    EXPECT_EQ(Bots::PickMeleeDamageTarget(context), Shaman);
    EXPECT_EQ(Bots::WhyNoDamageTarget(context, true), Bots::NoDamageTarget::None);
}

TEST(CasterPack, TheKeepOutRuleLetsAMeleeBotStayOnTheFocusThatTheTankHoldsThere)
{
    TankInOtherPackReach fight(AtTheShaman);
    fight.fake.SetVictim(fight.Rogue, Shaman);
    fight.fake.SetDistanceBetween(fight.Rogue, Shaman, 1.0f);
    Bots::BotContext const context = fight.Context();
    Bots::StrategyStack const stack = Bots::MakeStrategyStack(Bots::BotClass::Rogue, Bots::BotRole::Melee);

    // The rogue stands in the reach of pack11, with a place to step out to.
    ASSERT_TRUE(Bots::InOtherPacksReach(context, NearPack11));
    ASSERT_FALSE(Bots::StuckInOtherPacksReach(context, fight.Rogue));

    EXPECT_NE(stack.Think(context), Bots::KeepOutRule);
}

// Code review of the fix: the reach is the aggro distance and a margin, so
// the rogue that walks in to the focus enters it before melee contact. The
// keep-out rule lets it walk on: else it steps out, and the chase takes it
// back in.
TEST(CasterPack, TheKeepOutRuleLetsAMeleeBotWalkInToTheFocusThatTheTankHoldsThere)
{
    TankInOtherPackReach fight(AtTheShaman);
    fight.fake.SetVictim(fight.Rogue, Shaman);
    fight.fake.SetDistanceBetween(fight.Rogue, Shaman, 10.0f);
    Bots::BotContext const context = fight.Context();
    Bots::StrategyStack const stack = Bots::MakeStrategyStack(Bots::BotClass::Rogue, Bots::BotRole::Melee);

    EXPECT_NE(stack.Think(context), Bots::KeepOutRule);
}

// The exception is for melee members only: a ranged member next to the
// focus steps out, as it can cast from out of the reach.
TEST(CasterPack, TheKeepOutRuleStepsARangedMemberOutNextToTheFocusThatTheTankHoldsThere)
{
    TankInOtherPackReach fight(AtTheShaman);
    fight.group.SetMemberRole(fight.Rogue, Bots::BotRole::Ranged);
    fight.fake.SetVictim(fight.Rogue, Shaman);
    fight.fake.SetDistanceBetween(fight.Rogue, Shaman, 1.0f);
    Bots::BotContext const context = fight.Context();
    Bots::StrategyStack const stack = Bots::MakeStrategyStack(Bots::BotClass::Rogue, Bots::BotRole::Melee);

    EXPECT_EQ(stack.Think(context), Bots::KeepOutRule);
}

// The tank stands out of the reach: the melee bot on the focus steps out.
TEST(CasterPack, TheKeepOutRuleStepsAMeleeBotOutWhenTheTankStandsOutOfTheReach)
{
    TankInOtherPackReach fight(PullPack9);
    fight.fake.SetVictim(fight.Rogue, Shaman);
    fight.fake.SetDistanceBetween(fight.Rogue, Shaman, 1.0f);
    Bots::BotContext const context = fight.Context();
    Bots::StrategyStack const stack = Bots::MakeStrategyStack(Bots::BotClass::Rogue, Bots::BotRole::Melee);

    EXPECT_EQ(stack.Think(context), Bots::KeepOutRule);
}

// The tank stands out of the reach (the fake contact stays): the melee bot
// leaves the focus, and says why.
TEST(CasterPack, AMeleeBotLeavesTheFocusInTheReachOfAnotherPackWhenTheTankStandsOutOfIt)
{
    TankInOtherPackReach fight(PullPack9);
    Bots::BotContext const context = fight.Context();
    ASSERT_FALSE(Bots::InOtherPacksReach(context, PullPack9));

    EXPECT_EQ(Bots::PickMeleeDamageTarget(context), Bots::NoGuid);
    EXPECT_EQ(Bots::WhyNoDamageTarget(context, true), Bots::NoDamageTarget::InOtherPacksReach);
    // A ranged bot has no reach rule on its target.
    EXPECT_EQ(Bots::PickDamageTarget(context), Shaman);
}

// Full runs on b1e729403 (t93-final2): the tank switched 239 times between
// "walk back to the fight spot" (68) and "keep out of other packs" (82).
// One reach rule: the fight spot search chose the spot and the way back to
// it (and warned when it brings extra packs), so keep-out does not fire on
// the tank while it walks back to it. Here the walk back passes the place
// of pack11.
TEST(CasterPack, KeepOutDoesNotFireOnTheTanksWalkBackToItsFightSpot)
{
    CasterTank tank;
    tank.group.StartPull(Tank, Shaman);
    tank.Think();
    ASSERT_TRUE(tank.group.GetPull().hasFightSpot);
    tank.fake.SetPosition(Tank, NearPack11);
    tank.fake.SetInCombat(Tank);
    tank.fake.SetInCombat(Shaman);
    tank.fake.SetVictim(Shaman, Tank);
    tank.group.KeepTrail(NearPack11, true);
    tank.group.HoldPull(Tank, tank.nowMs);

    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(tank.Think(), "walk back to the fight spot");
}
