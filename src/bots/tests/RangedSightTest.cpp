#include "BotGeometry.h"
#include "ClassModules.h"
#include "DamageRules.h"
#include "FakeBot.h"
#include "MageModule.h"
#include "WarlockModule.h"

#include <gtest/gtest.h>

#include <sstream>

// jvbastel/WOWAI#160: a ranged damage bot with its target in casting range
// but out of sight walks to a spot that sees it, then casts.
using Bots::BotContext;
using Bots::GroupCoordinator;
using Bots::NoGuid;
using Bots::Position;
using Bots::StrategyStack;
using Bots::Tests::FakeBot;
namespace Mage = Bots::Mage;
namespace Warlock = Bots::Warlock;

namespace
{
    Bots::Guid const Tank = 0x0000000000000011;
    Bots::Guid const Self = 0x0000000000000014;
    Bots::Guid const Kobold = 0xF130000302000022;

    // The move of a deed "keep move to x y z", or false.
    bool MoveOf(std::string const& deed, Position& to)
    {
        std::istringstream in(deed);
        std::string keep, move, word;
        return in >> keep >> move >> word >> to.x >> to.y >> to.z && keep == "keep" && move == "move";
    }

    // A ranged damage bot 20 yards from a kobold that the tank holds, in
    // the fight. A wall stands between the bot and the kobold.
    struct RangedBot
    {
        FakeBot fake{Self};
        GroupCoordinator group;
        StrategyStack stack;

        explicit RangedBot(Bots::BotClass botClass)
            : stack(Bots::MakeStrategyStack(botClass, Bots::BotRole::Ranged))
        {
            for (Bots::SpellId spell : {Warlock::ShadowBolt, Warlock::Immolate, Warlock::Corruption,
                     Warlock::CurseOfAgony, Mage::Frostbolt, Mage::Fireball})
                fake.SetKnownSpell(spell);
            fake.SetHealth(Self, 400, 400);
            fake.SetPower(Self, 600, 600);
            fake.SetHealth(Kobold, 200, 200);
            fake.SetHealth(Tank, 500, 500);
            fake.SetPosition(Self, {-18, 0, 0});
            fake.SetPosition(Tank, {0, 0, 0});
            fake.SetPosition(Kobold, {2, 0, 0});
            fake.AddSightWall({-8, -4, 0}, {-8, 4, 0});
            group.StartPull(Tank, Kobold);
            fake.SetVictim(Kobold, Tank);
            Bots::Tests::HoldThreat(group, Tank);
            fake.SetInCombat(Self);
        }

        std::string Think()
        {
            BotContext const context{fake, fake, group, Bots::CommandState::None, NoGuid};
            return stack.Think(context);
        }

        void ExpectAMoveIntoSight()
        {
            ASSERT_FALSE(fake.CanSeeFrom(fake.GetPosition(Self), Kobold));
            EXPECT_EQ(Think(), Bots::TargetSightRuleName);
            ASSERT_EQ(fake.Deeds().size(), 1u);
            Position spot{};
            ASSERT_TRUE(MoveOf(fake.Deeds().front(), spot)) << fake.Deeds().front();
            EXPECT_TRUE(fake.CanSeeFrom(spot, Kobold));
            EXPECT_LE(Bots::Distance3d(spot, fake.GetPosition(Kobold)), Bots::RangedStandYards);
        }
    };
}

TEST(RangedSight, AWarlockWalksToASpotThatSeesItsTarget)
{
    RangedBot warlock(Bots::BotClass::Warlock);
    warlock.ExpectAMoveIntoSight();
}

TEST(RangedSight, AMageWalksToASpotThatSeesItsTarget)
{
    RangedBot mage(Bots::BotClass::Mage);
    mage.ExpectAMoveIntoSight();
}

TEST(RangedSight, ABotThatSeesItsTargetCasts)
{
    RangedBot mage(Bots::BotClass::Mage);
    mage.fake.SetPosition(Self, {-18, 10, 0});
    ASSERT_TRUE(mage.fake.CanSeeFrom(mage.fake.GetPosition(Self), Kobold));

    EXPECT_EQ(mage.Think(), "frostbolt");
}

TEST(RangedSight, ABotDoesNotStepWhileItCasts)
{
    RangedBot warlock(Bots::BotClass::Warlock);
    warlock.fake.SetCurrentCast(Self, Warlock::ShadowBolt);

    EXPECT_NE(warlock.Think(), Bots::TargetSightRuleName);
}

TEST(RangedSight, ABotWithNoSpotInSightChasesItsTarget)
{
    // No place sees the kobold, so no cast place is safe: the chase path of
    // the game ends in sight.
    RangedBot mage(Bots::BotClass::Mage);
    mage.fake.AddBlindToFromX(Kobold, -1000.0f);

    EXPECT_EQ(mage.Think(), Bots::TargetSightRuleName);
    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000022 to 25 yards"}));
}

TEST(RangedSight, AFarTargetIsLeftToTheCloseIn)
{
    RangedBot mage(Bots::BotClass::Mage);
    mage.fake.SetPosition(Self, {-38, 0, 0});

    EXPECT_EQ(mage.Think(), "run into casting range");
}
