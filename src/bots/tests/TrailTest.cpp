#include "Trail.h"
#include "GroupCoordinator.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

using Bots::GroupCoordinator;
using Bots::Position;
using Bots::Trail;
using Bots::Tests::FakeBot;

// The tank's trail: the ground it walked since the last fight, the cleared
// way of the fight spot search (FightSpot.h, jvbastel/WOWAI#105).
namespace
{
    Bots::Guid const Tank = 0x11;

    std::vector<float> Xs(std::vector<Position> const& points)
    {
        std::vector<float> xs;
        for (Position const& point : points)
            xs.push_back(point.x);
        return xs;
    }
}

TEST(Trail, KeepsAPointForEachStepOfTwoYards)
{
    Trail trail;
    for (float x : {0.0f, 1.0f, 2.0f, 3.0f, 4.5f, 5.0f, 7.0f})
        trail.Note({x, 0.0f, 0.0f});

    EXPECT_EQ(Xs(trail.Points()), (std::vector<float>{0.0f, 2.0f, 4.5f, 7.0f}));
}

TEST(Trail, AJumpLikeATeleportStartsANewTrail)
{
    Trail trail;
    trail.Note({0.0f, 0.0f, 0.0f});
    trail.Note({2.0f, 0.0f, 0.0f});
    trail.Note({100.0f, 0.0f, 0.0f});

    EXPECT_EQ(Xs(trail.Points()), (std::vector<float>{100.0f}));
}

TEST(Trail, KeepsOnlyTheNewestPointsOfALongWalk)
{
    Trail trail;
    for (int i = 0; i < 300; ++i)
        trail.Note({2.0f * float(i), 0.0f, 0.0f});

    ASSERT_EQ(trail.Points().size(), Bots::TrailMostPoints);
    EXPECT_EQ(trail.Points().back().x, 598.0f);
    EXPECT_EQ(trail.Points().front().x, 598.0f - 2.0f * float(Bots::TrailMostPoints - 1));
}

TEST(Trail, TheTankKeepsItsTrailInTheGroupCoordinatorOutOfCombat)
{
    FakeBot fake(Tank);
    fake.SetHealth(Tank, 600, 600);
    GroupCoordinator group;
    for (float x : {0.0f, 2.0f, 4.0f})
    {
        fake.SetPosition(Tank, {x, 0.0f, 0.0f});
        Bots::WatchTrail(fake, group);
    }

    EXPECT_EQ(Xs(group.GetTrail()), (std::vector<float>{0.0f, 2.0f, 4.0f}));
}

TEST(Trail, AFightStartsTheTrailAgainWhereTheTankStandsAfterIt)
{
    FakeBot fake(Tank);
    fake.SetHealth(Tank, 600, 600);
    GroupCoordinator group;
    fake.SetPosition(Tank, {0.0f, 0.0f, 0.0f});
    Bots::WatchTrail(fake, group);
    fake.SetPosition(Tank, {2.0f, 0.0f, 0.0f});
    Bots::WatchTrail(fake, group);

    fake.SetInCombat(Tank);
    fake.SetPosition(Tank, {6.0f, 0.0f, 0.0f});
    Bots::WatchTrail(fake, group);
    EXPECT_TRUE(group.GetTrail().empty());

    fake.SetInCombat(Tank, false);
    fake.SetPosition(Tank, {7.0f, 0.0f, 0.0f});
    Bots::WatchTrail(fake, group);
    EXPECT_EQ(Xs(group.GetTrail()), (std::vector<float>{7.0f}));
}

TEST(Trail, ADeadTankHasNoTrail)
{
    FakeBot fake(Tank);
    fake.SetHealth(Tank, 600, 600);
    GroupCoordinator group;
    fake.SetPosition(Tank, {0.0f, 0.0f, 0.0f});
    Bots::WatchTrail(fake, group);

    fake.SetLifeState(Tank, Bots::LifeState::Dead);
    fake.SetPosition(Tank, {2.0f, 0.0f, 0.0f});
    Bots::WatchTrail(fake, group);

    EXPECT_TRUE(group.GetTrail().empty());
}

// The review of jvbastel/WOWAI#100: the walk of the tank to its shoot spot
// during the group wait is no cleared way. While a pull is on, the trail
// stays as it was when the pull began.
TEST(Trail, APullFreezesTheTrail)
{
    FakeBot fake{Tank};
    fake.SetHealth(Tank, 600, 600);
    GroupCoordinator group;
    for (float x : {0.0f, 2.0f, 4.0f})
    {
        fake.SetPosition(Tank, {x, 0, 0});
        Bots::WatchTrail(fake, group);
    }
    group.StartPull(Tank, 0xF130000302000022);

    fake.SetPosition(Tank, {6, 0, 0});
    Bots::WatchTrail(fake, group);

    EXPECT_EQ(Xs(group.GetTrail()), (std::vector<float>{0, 2, 4}));
}
