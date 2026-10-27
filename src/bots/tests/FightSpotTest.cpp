#include "BotGeometry.h"
#include "FightSpot.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

using Bots::FightSpot;
using Bots::FindFightSpot;
using Bots::NearEnemy;
using Bots::Position;
using Bots::Tests::FakeBot;

// The fight spot of a pull (jvbastel/WOWAI#105): the tank steps back along
// its cleared way (Trail.h) to the first spot where no other pack joins.
namespace
{
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Target = 0xF130000302000022;
    Bots::Guid const Other = 0xF130000302000033;
    Bots::Guid const Third = 0xF130000302000044;
    Bots::Guid const Fourth = 0xF130000302000055;

    // A tank at (0, 0, 0) that walked along the x axis from x = -30, and a
    // pull target `targetX` yards ahead of it, in its own pack.
    struct Pull
    {
        FakeBot fake{Tank};
        std::vector<Position> trail;

        explicit Pull(float targetX = 25.0f)
        {
            fake.SetPosition(Tank, {0, 0, 0});
            for (float x = -30.0f; x <= 0.0f; x += 2.0f)
                trail.push_back({x, 0.0f, 0.0f});
            AddEnemy(Target, {targetX, 0, 0}, 10.0f);
        }

        void AddEnemy(Bots::Guid enemy, Position at, float aggroYards)
        {
            fake.SetPosition(enemy, at);
            fake.AddEnemy(enemy, at, aggroYards);
        }

        FightSpot Find() const { return FindFightSpot(fake, trail, Target); }
    };
}

TEST(Packs, EnemiesWithin8YardsOfEachOtherAreOnePack)
{
    std::vector<NearEnemy> const enemies = {
        {{0, 0, 0}, 10.0f, 1}, {{7, 0, 0}, 10.0f, 2}, {{14, 0, 0}, 10.0f, 3}, {{23.5f, 0, 0}, 10.0f, 4}};

    std::vector<std::vector<NearEnemy>> const packs = Bots::SplitIntoPacks(enemies);

    ASSERT_EQ(packs.size(), 2u);
    EXPECT_EQ(packs[0].size(), 3u);
    ASSERT_EQ(packs[1].size(), 1u);
    EXPECT_EQ(packs[1][0].guid, 4u);
}

TEST(FightSpot, WithNoOtherPackTheTankFightsWhereItStands)
{
    Pull pull;

    FightSpot const spot = pull.Find();

    EXPECT_TRUE(spot.clean);
    EXPECT_EQ(spot.spot.x, 0.0f);
    EXPECT_EQ(spot.extraPacks, 0u);
    EXPECT_FALSE(spot.hasShootSpot);
}

TEST(FightSpot, StepsBackToTheFirstSpotOutOfTheAggroRangeAndMarginOfAnotherPack)
{
    Pull pull;
    // Aggro 10 and the 5 yard margin: 15 yards. x = -6 is 13.6 yards away,
    // x = -8 is 15.3.
    pull.AddEnemy(Other, {5, 8, 0}, 10.0f);

    FightSpot const spot = pull.Find();

    EXPECT_TRUE(spot.clean);
    EXPECT_EQ(spot.spot.x, -8.0f);
    EXPECT_EQ(spot.extraPacks, 0u);
}

TEST(FightSpot, UsesTheOwnAggroDistanceOfEachEnemy)
{
    Pull pull;
    // Aggro 14: 19 yards. x = -12 is 18.8 yards away, x = -14 is 21.
    pull.AddEnemy(Other, {5, 8, 0}, 14.0f);

    EXPECT_EQ(pull.Find().spot.x, -14.0f);
}

TEST(FightSpot, KeepsMoreThanTheAssistanceRadiusFromAnotherPackWithASmallAggroRange)
{
    Pull pull;
    // Aggro 2 and the margin: 7 yards. But the 10 yards of the assistance
    // radius: x = -8 is 10 yards away, x = -10 is 11.7.
    pull.AddEnemy(Other, {0, 6, 0}, 2.0f);

    FightSpot const spot = pull.Find();

    EXPECT_TRUE(spot.clean);
    EXPECT_EQ(spot.spot.x, -10.0f);
}

TEST(FightSpot, OnlyTheOtherPacksCountNotThePullTarget)
{
    Pull pull;
    // A second mob of the target's own pack, 5 yards from it.
    pull.AddEnemy(Third, {20, 0, 0}, 20.0f);

    FightSpot const spot = pull.Find();

    EXPECT_TRUE(spot.clean);
    EXPECT_EQ(spot.spot.x, 0.0f);
}

TEST(FightSpot, WithNoCleanSpotTakesTheNewestOfTheSpotsThatBringTheFewestExtraPacks)
{
    Pull pull;
    // Each spot brings a pack: Other (aggro 20 + 5) reaches x = 0 to -18,
    // Third (10 yards: the assistance radius) x = 0 and -2, Fourth (aggro 5 + 5) x = -18 to -30.
    // From x = -4 to -16 it is Other alone.
    pull.AddEnemy(Other, {5, 6, 0}, 20.0f);
    pull.AddEnemy(Third, {4, -8, 0}, 5.0f);
    pull.AddEnemy(Fourth, {-26, 6, 0}, 5.0f);

    FightSpot const spot = pull.Find();

    EXPECT_FALSE(spot.clean);
    EXPECT_EQ(spot.extraPacks, 1u);
    EXPECT_EQ(spot.spot.x, -4.0f);
}

TEST(FightSpot, APackWithinTheAssistanceRadiusOfTheTargetJoinsWhereverTheTankFights)
{
    Pull pull;
    // 9.5 yards from the target: it answers the target's call for help.
    pull.AddEnemy(Other, {25, 9.5f, 0}, 10.0f);

    FightSpot const spot = pull.Find();

    EXPECT_FALSE(spot.clean);
    EXPECT_EQ(spot.extraPacks, 1u);
    EXPECT_EQ(spot.spot.x, 0.0f);
}

TEST(FightSpot, PullsFromTheNewestSpotOfTheTrailThatSeesTheTarget)
{
    Pull pull;
    // The tank does not see the target from x >= -3.
    pull.fake.AddBlindToFromX(Target, -3.0f);

    FightSpot const spot = pull.Find();

    EXPECT_TRUE(spot.clean);
    ASSERT_TRUE(spot.hasShootSpot);
    EXPECT_EQ(spot.shootSpot.x, -4.0f);
}

TEST(FightSpot, WithNoTrailSpotInRangePullsFromASafePlaceRoundTheTarget)
{
    // The target is 40 yards away, out of shot from the whole trail.
    Pull pull(40.0f);

    FightSpot const spot = pull.Find();

    EXPECT_TRUE(spot.clean);
    ASSERT_TRUE(spot.hasShootSpot);
    // On the ring of 0.9 of the shot range, nearest to the tank.
    EXPECT_NEAR(spot.shootSpot.x, 13.0f, 0.01f);
    EXPECT_EQ(spot.spot.x, 0.0f);
}

TEST(FightSpot, NoShotFromAnywhereIsNoCleanSpot)
{
    Pull pull;
    pull.fake.AddBlindToFromX(Target, -1000.0f);

    FightSpot const spot = pull.Find();

    EXPECT_FALSE(spot.clean);
    EXPECT_FALSE(spot.hasShootSpot);
    EXPECT_EQ(spot.extraPacks, 0u);
    EXPECT_EQ(spot.spot.x, 0.0f);
}

TEST(FightSpot, AShootSpotOnTheTrailPullsNoOtherPack)
{
    Pull pull(20.0f);
    pull.fake.AddBlindToFromX(Target, -3.0f);
    // x = -4 and -6 are in the reach of pack Other (aggro 2 + 5), -8 is not.
    pull.AddEnemy(Other, {-3, 6, 0}, 2.0f);

    FightSpot const spot = pull.Find();

    ASSERT_TRUE(spot.hasShootSpot);
    EXPECT_EQ(spot.shootSpot.x, -8.0f);
}

TEST(FightSpot, TrailSpotsFarFromTheTankDoNotCount)
{
    Pull pull;
    pull.trail.clear();
    for (float x = -100.0f; x <= 0.0f; x += 2.0f)
        pull.trail.push_back({x, 0.0f, 0.0f});
    // Every spot nearer than 40 yards is in the reach of pack Other; the
    // spots from x = -56 on are not, but they are too far.
    pull.AddEnemy(Other, {-20, 0, 0}, 30.0f);

    FightSpot const spot = pull.Find();

    EXPECT_FALSE(spot.clean);
    EXPECT_EQ(spot.spot.x, 0.0f);
}
