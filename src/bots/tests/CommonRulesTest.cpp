#include "CommonRules.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

using Bots::SeenDead;
using Bots::Tests::FakeBot;

namespace
{
    Bots::Guid const Self = 0x12;
    Bots::Guid const Enemy = 0x900;
}

TEST(SeenDead, AUnitAtZeroHealthIsSeenDead)
{
    FakeBot fake(Self);
    fake.SetHealth(Enemy, 0, 800);

    EXPECT_TRUE(SeenDead(fake, Enemy));
}

TEST(SeenDead, ALivingUnitIsNotSeenDead)
{
    FakeBot fake(Self);
    fake.SetHealth(Enemy, 1, 800);

    EXPECT_FALSE(SeenDead(fake, Enemy));
}

// A unit on another map, or out of the bot's sight, has no health for the
// bot: the bot does not take it for dead.
TEST(SeenDead, AUnitThatTheBotDoesNotFindIsNotSeenDead)
{
    FakeBot fake(Self);

    EXPECT_FALSE(SeenDead(fake, Enemy));
}

// Spec #188 fix-up (standards 3): the one lookup of the player tank. A tank
// that the bot does not find (no health: another map, a loading screen) is
// no player tank to act on.
namespace
{
    Bots::Guid const Human = 0x1;
}

TEST(PlayerTankOf, IsThePlayerTankThatTheBotSees)
{
    FakeBot fake(Self);
    Bots::GroupCoordinator group;
    group.SetTank(Human, true);
    fake.SetHealth(Human, 600, 600);

    EXPECT_EQ(Bots::PlayerTankOf(fake, group), Human);
    // Seen dead too.
    fake.SetHealth(Human, 0, 600);
    EXPECT_EQ(Bots::PlayerTankOf(fake, group), Human);
}

TEST(PlayerTankOf, AnUnseenTankIsNone)
{
    FakeBot fake(Self);
    Bots::GroupCoordinator group;
    group.SetTank(Human, true);

    EXPECT_EQ(Bots::PlayerTankOf(fake, group), Bots::NoGuid);
}

TEST(PlayerTankOf, ABotTankGroupHasNone)
{
    FakeBot fake(Self);
    Bots::GroupCoordinator group;
    group.SetTank(Human);
    fake.SetHealth(Human, 600, 600);

    EXPECT_EQ(Bots::PlayerTankOf(fake, group), Bots::NoGuid);
}
