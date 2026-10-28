#include "BotGeometry.h"
#include "ClassModules.h"
#include "DamageRules.h"
#include "FakeBot.h"
#include "MageModule.h"
#include "PriestModule.h"
#include "StepBack.h"
#include "WarlockModule.h"

#include <gtest/gtest.h>

using Bots::BotContext;
using Bots::BotRole;
using Bots::Distance3d;
using Bots::GroupCoordinator;
using Bots::Position;
using Bots::Tests::FakeBot;

// jvbastel/WOWAI#144: the step back. A ranged member or the healer within
// StepBackYards of an enemy frozen by Frost Nova walks back along the
// cleared way of the fight until it stands StepBackYards from every frozen
// enemy, walking at most StepBackMostYards; else it goes to the nearest
// safe place (SafePlace.h).
namespace
{
    Bots::Guid const Self = 0x13;
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Kobold = 0xF130000302000022;
    Bots::Guid const Wolf = 0xF130000302000033;
    Bots::Guid const Bat = 0xF130000302000044;

    // The fight on the x axis: the tank came from x = -40 along the axis
    // and fights the kobold at (0, 0, 0). The bot stands back on the
    // cleared way at (-10, 0, 0).
    struct Fight
    {
        FakeBot fake{Self};
        GroupCoordinator group;

        Fight()
        {
            fake.SetGroupMembers({Tank});
            fake.SetHealth(Self, 400, 400);
            fake.SetHealth(Tank, 600, 600);
            fake.SetPosition(Self, {-10, 0, 0});
            fake.SetPosition(Tank, {0, 0, 0});
            group.SetTank(Tank);
            for (float x = -40.0f; x <= 0.0f; x += 2.0f)
                group.KeepTrail({x, 0, 0}, false);
            group.KeepTrail({0, 0, 0}, true);
            AddEnemy(Kobold, {1, 0, 0});
            fake.SetVictim(Kobold, Tank);
            fake.SetVictim(Tank, Kobold);
            fake.SetInCombat(Self);
            group.SetFocusTarget(Kobold);
        }

        void AddEnemy(Bots::Guid enemy, Position at)
        {
            fake.SetPosition(enemy, at);
            fake.AddEnemy(enemy, at, 10.0f);
        }

        // Enemy `enemy` at `at`, frozen by Frost Nova, on member `victim`.
        void AddFrozen(Bots::Guid enemy, Position at, Bots::Guid victim = Self)
        {
            AddEnemy(enemy, at);
            fake.SetAura(enemy, Bots::Mage::FrostNova);
            fake.SetVictim(enemy, victim);
        }

        bool Find(BotRole role, Position& spot)
        {
            BotContext const context{fake, fake, group, Bots::CommandState::None, Bots::NoGuid};
            return Bots::FindStepBackSpot(context, role, spot);
        }
    };

    ::testing::AssertionResult OutOfReachOfFrozen(FakeBot const& fake, Position const& spot,
        std::vector<Bots::Guid> const& frozen)
    {
        for (Bots::Guid enemy : frozen)
        {
            float const yards = Distance3d(spot, fake.GetPosition(enemy));
            if (yards < Bots::StepBackYards)
                return ::testing::AssertionFailure()
                    << "spot " << spot.x << ' ' << spot.y << " is " << yards << " yards from a frozen enemy";
        }
        return ::testing::AssertionSuccess();
    }
}

TEST(StepBack, WalksBackAlongTheClearedWayToEightYardsFromTheFrozenEnemy)
{
    Fight fight;
    fight.AddFrozen(Wolf, {-5, 0, 0});
    Position spot{};

    ASSERT_TRUE(fight.Find(BotRole::Ranged, spot));

    // The first point of the way back 8 yards or more from the wolf.
    EXPECT_EQ(spot.x, -14.0f);
    EXPECT_EQ(spot.y, 0.0f);
}

TEST(StepBack, WalksBackUntilItIsEightYardsFromEveryFrozenEnemy)
{
    Fight fight;
    fight.AddFrozen(Wolf, {-5, 0, 0});
    fight.AddFrozen(Bat, {-12, 6, 0});
    Position spot{};

    ASSERT_TRUE(fight.Find(BotRole::Ranged, spot));

    EXPECT_EQ(spot.x, -18.0f);
    EXPECT_EQ(spot.y, 0.0f);
}

// No point of the way within 15 yards of walk is out of reach of both
// enemies: the bot goes to the nearest safe place instead.
TEST(StepBack, GoesToTheNearestSafePlaceWhenTheWayBackIsTooLong)
{
    Fight fight;
    fight.AddFrozen(Wolf, {-5, 0, 0});
    fight.AddFrozen(Bat, {-20, 0, 0}, Tank);
    Position spot{};

    ASSERT_TRUE(fight.Find(BotRole::Ranged, spot));

    EXPECT_TRUE(OutOfReachOfFrozen(fight.fake, spot, {Wolf, Bat}));
    EXPECT_NE(spot.y, 0.0f);
    EXPECT_LE(Distance3d(spot, {-10, 0, 0}), Bots::StepBackYards + 0.01f);
}

// A frozen enemy stands on the way back: the bot does not walk past it.
TEST(StepBack, GoesToTheNearestSafePlaceWhenAFrozenEnemyStandsOnTheWayBack)
{
    Fight fight;
    fight.AddFrozen(Wolf, {-13, 0, 0});
    Position spot{};

    ASSERT_TRUE(fight.Find(BotRole::Ranged, spot));

    EXPECT_TRUE(OutOfReachOfFrozen(fight.fake, spot, {Wolf}));
    EXPECT_GT(spot.x, -13.0f);
}

TEST(StepBack, GoesToTheNearestSafePlaceWithoutAClearedWay)
{
    Fight fight;
    fight.group.KeepTrail({0, 0, 0}, false);
    fight.group.KeepTrail({0, 0, 0}, true);
    ASSERT_TRUE(fight.group.GetFightWay().size() <= 1);
    fight.AddFrozen(Wolf, {-5, 0, 0});
    Position spot{};

    ASSERT_TRUE(fight.Find(BotRole::Ranged, spot));

    EXPECT_TRUE(OutOfReachOfFrozen(fight.fake, spot, {Wolf}));
}

// The way back leaves the healer's sight of the tank: it takes a safe place
// that sees the tank.
TEST(StepBack, TheHealerKeepsSightOfTheTank)
{
    Fight fight;
    fight.AddFrozen(Wolf, {-5, 0, 0});
    fight.fake.AddBlindToBelowX(Tank, -13.0f);
    Position spot{};

    ASSERT_TRUE(fight.Find(BotRole::Healer, spot));

    EXPECT_TRUE(OutOfReachOfFrozen(fight.fake, spot, {Wolf}));
    EXPECT_TRUE(fight.fake.CanSeeFrom(spot, Tank));
}

// A ranged damage member needs no sight of the tank: it takes the way back.
TEST(StepBack, ARangedMemberNeedsNoSightOfTheTank)
{
    Fight fight;
    fight.AddFrozen(Wolf, {-5, 0, 0});
    fight.fake.AddBlindToBelowX(Tank, -13.0f);
    Position spot{};

    ASSERT_TRUE(fight.Find(BotRole::Ranged, spot));

    EXPECT_EQ(spot.x, -14.0f);
}

// The tank fights 24 yards ahead: the way back would leave the heal reach of
// the tank, so the healer steps aside instead.
TEST(StepBack, TheHealerKeepsHealingRangeOfTheTank)
{
    Fight fight;
    fight.fake.SetPosition(Tank, {24, 0, 0});
    fight.AddFrozen(Wolf, {-5, 0, 0});
    Position spot{};

    ASSERT_TRUE(fight.Find(BotRole::Healer, spot));

    EXPECT_TRUE(OutOfReachOfFrozen(fight.fake, spot, {Wolf}));
    EXPECT_LE(Distance3d(spot, {24, 0, 0}), Bots::StepBackHealYards);
}

// A pack outside the fight waits beside the way back: the bot keeps out of
// its reach (KeepOut.h).
TEST(StepBack, KeepsOutOfThePacksOutsideTheFight)
{
    Fight fight;
    fight.AddFrozen(Wolf, {-5, 0, 0});
    // In no combat; its reach (10 + 5 yards) covers the way at x = -14.
    fight.AddEnemy(Bat, {-14, 14, 0});
    Position spot{};

    ASSERT_TRUE(fight.Find(BotRole::Ranged, spot));

    EXPECT_TRUE(OutOfReachOfFrozen(fight.fake, spot, {Wolf}));
    EXPECT_GE(Distance3d(spot, {-14, 14, 0}), 15.0f);
}

TEST(StepBack, NoSpotWithoutAFrozenEnemyInReach)
{
    Fight fight;
    fight.AddFrozen(Wolf, {-1, 0, 0});
    Position spot{};

    EXPECT_FALSE(fight.Find(BotRole::Ranged, spot));
}

namespace
{
    // The step back as a rule of a class module.
    struct Member : Fight
    {
        Bots::StrategyStack stack;

        Member(Bots::BotClass botClass, BotRole role) : stack(Bots::MakeStrategyStack(botClass, role))
        {
            fake.SetPower(Self, 600, 600);
            fake.SetPowerKind(Self, Bots::PowerKind::Mana);
            Bots::Tests::HoldThreat(group, Tank);
        }

        std::string Think()
        {
            BotContext const context{fake, fake, group, Bots::CommandState::None, Bots::NoGuid};
            return stack.Think(context);
        }
    };

    std::string const MoveToTheWayBack = "keep move to -14 0 0";
}

TEST(StepBack, TheMageStepsBack)
{
    Member mage(Bots::BotClass::Mage, BotRole::Ranged);
    mage.fake.SetKnownSpell(Bots::Mage::Frostbolt);
    mage.AddFrozen(Wolf, {-5, 0, 0});

    EXPECT_EQ(mage.Think(), Bots::StepBackRuleName);
    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{MoveToTheWayBack}));
}

TEST(StepBack, TheWarlockStepsBack)
{
    Member warlock(Bots::BotClass::Warlock, BotRole::Ranged);
    warlock.fake.SetKnownSpell(Bots::Warlock::ShadowBolt);
    warlock.AddFrozen(Wolf, {-5, 0, 0});

    EXPECT_EQ(warlock.Think(), Bots::StepBackRuleName);
    EXPECT_EQ(warlock.fake.Deeds(), (std::vector<std::string>{MoveToTheWayBack}));
}

TEST(StepBack, TheHealerStepsBack)
{
    Member healer(Bots::BotClass::Priest, BotRole::Healer);
    healer.fake.SetKnownSpell(Bots::Priest::LesserHeal);
    healer.AddFrozen(Wolf, {-5, 0, 0});

    EXPECT_EQ(healer.Think(), Bots::StepBackRuleName);
    EXPECT_EQ(healer.fake.Deeds(), (std::vector<std::string>{MoveToTheWayBack}));
}

TEST(StepBack, AMemberAlreadyEightYardsAwayDoesNotMove)
{
    Member mage(Bots::BotClass::Mage, BotRole::Ranged);
    mage.fake.SetKnownSpell(Bots::Mage::Frostbolt);
    mage.AddFrozen(Wolf, {-1.5f, 0, 0}, Tank);

    EXPECT_NE(mage.Think(), Bots::StepBackRuleName);
}

// The enemy near the bot is not frozen: no step back (the peel nova or the
// tank deal with it).
TEST(StepBack, NoStepBackFromAnEnemyThatIsNotFrozen)
{
    Member mage(Bots::BotClass::Mage, BotRole::Ranged);
    mage.fake.SetKnownSpell(Bots::Mage::Frostbolt);
    mage.AddEnemy(Wolf, {-5, 0, 0});
    mage.fake.SetVictim(Wolf, Tank);

    EXPECT_NE(mage.Think(), Bots::StepBackRuleName);
}

// After the step back, the bot does its job from where it stands: no walk
// back to its old place.
TEST(StepBack, AfterTheStepBackTheMageCastsFromWhereItStands)
{
    Member mage(Bots::BotClass::Mage, BotRole::Ranged);
    mage.fake.SetKnownSpell(Bots::Mage::Frostbolt);
    mage.AddFrozen(Wolf, {-5, 0, 0}, Tank);
    mage.fake.SetPosition(Self, {-14, 0, 0});

    mage.Think();

    EXPECT_EQ(mage.fake.Deeds().size(), 1u);
    EXPECT_EQ(mage.fake.Deeds().front().rfind("cast ", 0), 0u);
}
