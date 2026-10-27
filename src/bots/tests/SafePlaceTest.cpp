#include "BotGeometry.h"
#include "SafePlace.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

#include <cmath>

using Bots::FindSafeCastPlace;
using Bots::FirstSafePlace;
using Bots::Position;
using Bots::SafeMoveRules;
using Bots::Tests::FakeBot;

// The safe place of a bot: a place to move to on a path with no harmful
// ground (lava), that pulls no enemy, and from which a unit is in sight.
// Shared by the Priest's resurrect walk (WOWAI#72) and the Fire Nova move
// of Taragaman (WOWAI#74).
namespace
{
    Bots::Guid const Self = 0x12;
    Bots::Guid const Corpse = 0x13;
    Bots::Guid const Enemy = 0x900;

    // The bot 40 yards south of a corpse at the origin.
    struct Walker
    {
        FakeBot fake;

        Walker() : fake(Self)
        {
            fake.SetPosition(Self, {0.0f, -40.0f, 0.0f});
            fake.SetPosition(Corpse, {0.0f, 0.0f, 0.0f});
        }

        bool Find(Position& place, float yards = 30.0f) const
        {
            return FindSafeCastPlace(fake, Corpse, yards, place);
        }
    };

    void ExpectAt(Position const& place, float x, float y)
    {
        EXPECT_NEAR(place.x, x, 0.01f);
        EXPECT_NEAR(place.y, y, 0.01f);
    }
}

TEST(SafeCastPlace, WithNothingAroundThePlaceIsOnTheOuterRingNearestToTheBot)
{
    Walker walker;
    Position place{};

    ASSERT_TRUE(walker.Find(place));
    // 0.9 of the 30 yards, on the line to the bot.
    ExpectAt(place, 0.0f, -27.0f);
}

TEST(SafeCastPlace, APlaceInLavaDoesNotCount)
{
    Walker walker;
    // Lava under the outer ring spot south of the corpse only.
    walker.fake.AddLavaArea(-3.0f, 3.0f, -29.0f, -25.0f);
    Position place{};

    ASSERT_TRUE(walker.Find(place));
    // The next nearest spots: the outer ring, 22.5 degrees off south.
    EXPECT_NEAR(std::fabs(place.x), 10.33f, 0.01f);
    EXPECT_NEAR(place.y, -24.94f, 0.01f);
}

TEST(SafeCastPlace, APathThroughLavaDoesNotCountThoughThePlaceIsSafe)
{
    Walker walker;
    // A lava strip across the way, between the bot and the corpse's rings.
    walker.fake.AddLavaArea(-100.0f, 100.0f, -35.0f, -33.0f);
    Position place{};

    EXPECT_FALSE(walker.Find(place));
}

TEST(SafeCastPlace, APlaceOutOfSightOfTheCorpseDoesNotCount)
{
    Walker walker;
    walker.fake.SetPosition(Self, {40.0f, 0.0f, 0.0f});
    // From the east half, the corpse is out of sight.
    walker.fake.AddBlindFromX(1.0f);
    Position place{};

    ASSERT_TRUE(walker.Find(place));
    EXPECT_LT(place.x, 1.0f);
}

TEST(SafeCastPlace, APlaceFarAboveOrUnderTheCorpseDoesNotCount)
{
    Walker walker;
    // A ledge 10 yards up east of x = 1.
    walker.fake.SetPosition(Self, {40.0f, 0.0f, 0.0f});
    walker.fake.AddGroundFromX(1.0f, 10.0f);
    Position place{};

    ASSERT_TRUE(walker.Find(place));
    EXPECT_LT(place.x, 1.0f);
    EXPECT_FLOAT_EQ(place.z, 0.0f);
}

TEST(SafeCastPlace, APlaceWithNoGroundPathDoesNotCount)
{
    Walker walker;
    walker.fake.SetPosition(Self, {40.0f, 0.0f, 0.0f});
    walker.fake.AddNoPathFromX(1.0f);
    Position place{};

    ASSERT_TRUE(walker.Find(place));
    EXPECT_LT(place.x, 1.0f);
}

TEST(SafeCastPlace, APlaceNearAnEnemyDoesNotCount)
{
    Walker walker;
    // Aggro 5 yards and the margin of 5: the outer ring spot south of the
    // corpse is 7 yards from it.
    walker.fake.AddEnemy(Enemy, {7.0f, -27.0f, 0.0f}, 5.0f);
    Position place{};

    ASSERT_TRUE(walker.Find(place));
    EXPECT_GE(Bots::Distance2d(place, {7.0f, -27.0f, 0.0f}), 10.0f);
}

TEST(SafeCastPlace, AWalkAwayFromAnEnemyTheBotStandsNearIsFine)
{
    Walker walker;
    walker.fake.SetPosition(Self, {0.0f, -30.0f, 0.0f});
    // 8 yards behind the bot: inside the aggro distance and the margin, but
    // the walk goes no nearer to it.
    walker.fake.AddEnemy(Enemy, {0.0f, -38.0f, 0.0f}, 5.0f);
    Position place{};

    ASSERT_TRUE(walker.Find(place));
    ExpectAt(place, 0.0f, -27.0f);
}

TEST(SafeCastPlace, AWalkThatPassesNearAnEnemyDoesNotCount)
{
    Walker walker;
    // East of the way: the walk south to north would pass 6 yards from it,
    // inside its aggro distance of 3 and the margin. The two nearest spots
    // are south and south-east; the walk to the south-west spot keeps away.
    walker.fake.AddEnemy(Enemy, {6.0f, -33.0f, 0.0f}, 3.0f);
    Position place{};

    ASSERT_TRUE(walker.Find(place));
    ExpectAt(place, -10.33f, -24.94f);
}

TEST(SafeCastPlace, ACorpseTooFarToKnowTheEnemiesOnTheWayHasNoPlace)
{
    Walker walker;
    walker.fake.SetPosition(Self, {0.0f, -100.0f, 0.0f});
    Position place{};

    EXPECT_FALSE(walker.Find(place));
}

TEST(SafeCastPlace, TheBotLooksForEnemiesAroundTheWholeWay)
{
    Walker walker;
    walker.fake.SetPosition(Self, {0.0f, -40.0f, 0.0f});
    // Near the corpse, 40 yards from the bot: a pack with a long aggro
    // distance covers the whole cast range.
    walker.fake.AddEnemy(Enemy, {0.0f, 0.0f, 0.0f}, 40.0f);
    Position place{};

    EXPECT_FALSE(walker.Find(place));
}

TEST(FirstSafePlace, TakesTheSpotsInTheirOrder)
{
    FakeBot fake(Self);
    fake.SetPosition(Self, {0.0f, 0.0f, 0.0f});
    fake.AddLavaArea(4.0f, 6.0f, -1.0f, 1.0f);
    Position place{};

    // The first spot is behind the lava, the second is not.
    ASSERT_TRUE(FirstSafePlace(fake, {{10.0f, 0.0f, 0.0f}, {0.0f, 10.0f, 0.0f}, {-10.0f, 0.0f, 0.0f}},
        SafeMoveRules{}, place));
    ExpectAt(place, 0.0f, 10.0f);
}

TEST(FirstSafePlace, WithNoSafeSpotThereIsNoPlace)
{
    FakeBot fake(Self);
    fake.SetPosition(Self, {0.0f, 0.0f, 0.0f});
    fake.AddLavaFromX(-100.0f);
    Position place{};

    EXPECT_FALSE(FirstSafePlace(fake, {{10.0f, 0.0f, 0.0f}}, SafeMoveRules{}, place));
}

// The one aggro rule: the game pulls a creature when a player comes within
// its aggro distance of its center (BasicAI::MoveInLineOfSight, no body
// size), so a safe place keeps the aggro distance and the margin from the
// enemy's center. The bot's body and the enemy's body do not count.
TEST(FirstSafePlace, TheAggroMarginIsMeasuredFromTheEnemysCenter)
{
    FakeBot fake(Self);
    fake.SetPosition(Self, {0.0f, 0.0f, 0.0f});
    // Aggro 5 and the margin of 5: 10 yards from its center.
    SafeMoveRules rules;
    rules.enemies = {{{20.0f, 0.0f, 0.0f}, 5.0f}};
    Position place{};

    ASSERT_TRUE(FirstSafePlace(fake, {{10.5f, 0.0f, 0.0f}, {9.5f, 0.0f, 0.0f}}, rules, place));
    ExpectAt(place, 9.5f, 0.0f);
}

// A place that the caller's rule refuses (a shot too near, Ranged.h) does
// not count: the next ring does.
TEST(SafeCastPlace, OnlyAPlaceThatTheCallerAccepts)
{
    Walker walker;
    // Next to the corpse: the inner ring is nearest.
    walker.fake.SetPosition(Self, {0.0f, -1.0f, 0.0f});
    Position place{};

    ASSERT_TRUE(walker.Find(place));
    ExpectAt(place, 0.0f, -9.0f);
    ASSERT_TRUE(FindSafeCastPlace(walker.fake, Corpse, 30.0f, place, nullptr,
        [](Position const& at) { return std::fabs(at.y) > 20.0f; }));
    ExpectAt(place, 0.0f, -27.0f);
}
