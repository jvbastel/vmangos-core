#include "BotGeometry.h"
#include "FakeBot.h"
#include "MemberSpot.h"

#include <gtest/gtest.h>

using Bots::BotRole;
using Bots::Distance3d;
using Bots::Position;
using Bots::Tests::FakeBot;

// The member spots of a pull (jvbastel/WOWAI#106): before the tank pulls,
// each other bot goes to a spot of its own near the fight spot.
namespace
{
    Bots::Guid const Self = 0x12;
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Kobold = 0xF130000302000022;
    Bots::Guid const Wolf = 0xF130000302000033;

    // The tank's fight spot at (0, 0, 0), on its trail along the x axis from
    // x = -30, and the kobold 25 yards ahead. The bot stands at the start of
    // the trail.
    struct Spots
    {
        FakeBot fake{Self};
        std::vector<Position> trail;
        Position const fightSpot{0, 0, 0};

        Spots()
        {
            fake.SetPosition(Self, {-30, 0, 0});
            fake.SetPosition(Tank, {0, 0, 0});
            for (float x = -30.0f; x <= 0.0f; x += 2.0f)
                trail.push_back({x, 0.0f, 0.0f});
            AddEnemy(Kobold, {25, 0, 0}, 10.0f);
        }

        void AddEnemy(Bots::Guid enemy, Position at, float aggroYards)
        {
            fake.SetPosition(enemy, at);
            fake.AddEnemy(enemy, at, aggroYards);
        }

        bool Find(BotRole role, Position& spot, std::vector<Position> const& taken = {}) const
        {
            return Bots::FindMemberSpot(fake, role, fightSpot, trail, Tank, Kobold, taken, spot);
        }
    };

    ::testing::AssertionResult BackFromTheFightSpot(Position const& spot)
    {
        float const yards = Distance3d(spot, {0, 0, 0});
        if (yards >= Bots::MemberBackMinYards && yards <= Bots::MemberBackMaxYards && spot.x < 0)
            return ::testing::AssertionSuccess();
        return ::testing::AssertionFailure() << "spot " << spot.x << ' ' << spot.y << " is " << yards
                                             << " yards from the fight spot";
    }
}

TEST(MemberSpot, RangedStandsFifteenToTwentyFiveYardsBackOnTheClearedWay)
{
    Spots spots;
    Position spot{};

    ASSERT_TRUE(spots.Find(BotRole::Ranged, spot));

    // The first trail point 15 yards or more back.
    EXPECT_EQ(spot.x, -16.0f);
    EXPECT_EQ(spot.y, 0.0f);
}

TEST(MemberSpot, RangedAndHealerSpreadFiveYardsFromTheOtherMembers)
{
    Spots spots;
    std::vector<Position> taken;
    for (BotRole role : {BotRole::Ranged, BotRole::Healer, BotRole::Ranged, BotRole::Ranged})
    {
        Position spot{};
        ASSERT_TRUE(spots.Find(role, spot, taken));
        EXPECT_TRUE(BackFromTheFightSpot(spot));
        for (Position const& other : taken)
            EXPECT_GE(Distance3d(spot, other), Bots::MemberSpreadYards);
        taken.push_back(spot);
    }
}

TEST(MemberSpot, WithoutATrailTheSpotIsBackAwayFromTheTarget)
{
    Spots spots;
    spots.trail.clear();
    Position spot{};

    ASSERT_TRUE(spots.Find(BotRole::Ranged, spot));

    EXPECT_EQ(spot.x, -20.0f);
    EXPECT_NEAR(spot.y, 0.0f, 0.01f);
}

TEST(MemberSpot, TheHealersSpotSeesTheTank)
{
    Spots spots;
    // From x = -20 on, a wall hides the tank.
    spots.fake.AddBlindToFromX(Tank, -20.0f);
    Position ranged{};
    Position healer{};

    ASSERT_TRUE(spots.Find(BotRole::Ranged, ranged));
    ASSERT_TRUE(spots.Find(BotRole::Healer, healer));

    EXPECT_EQ(ranged.x, -16.0f);
    EXPECT_LT(healer.x, -20.0f);
    EXPECT_TRUE(BackFromTheFightSpot(healer));
}

TEST(MemberSpot, TheSpotKeepsOutOfTheAggroOfAnotherPackWithTheMargin)
{
    Spots spots;
    // Aggro 10 and the margin 5: x = -16 (14.1 yards) and -18 (14.6) pull it.
    spots.AddEnemy(Wolf, {-14, 14, 0}, 10.0f);
    Position spot{};

    ASSERT_TRUE(spots.Find(BotRole::Ranged, spot));

    EXPECT_EQ(spot.x, -20.0f);
}

TEST(MemberSpot, MeleeStandsAtTheFightSpotEvenInTheAggroOfThePulledPack)
{
    Spots spots;
    spots.AddEnemy(Kobold, {25, 0, 0}, 30.0f);
    Position spot{};

    ASSERT_TRUE(spots.Find(BotRole::Melee, spot));

    EXPECT_EQ(spot.x, -Bots::MeleeBackYards);
    EXPECT_EQ(spot.y, 0.0f);
}

TEST(MemberSpot, NoSpotWhenNoneIsSafe)
{
    Spots spots;
    spots.fake.AddLavaFromX(-1000.0f);
    Position spot{};

    EXPECT_FALSE(spots.Find(BotRole::Ranged, spot));
    EXPECT_FALSE(spots.Find(BotRole::Melee, spot));
}
