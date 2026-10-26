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
