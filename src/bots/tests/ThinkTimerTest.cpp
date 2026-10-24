#include "ThinkTimer.h"

#include <gtest/gtest.h>

using Bots::ThinkSettings;
using Bots::ThinkState;
using Bots::ThinkTimer;

TEST(ThinkTimer, ThinksAtTheFirstUpdate)
{
    ThinkTimer timer;

    EXPECT_TRUE(timer.Advance(0, ThinkState::Far));
}

TEST(ThinkTimer, InCombatThinksAbout10TimesASecond)
{
    ThinkTimer timer;
    timer.Advance(0, ThinkState::Combat);

    EXPECT_FALSE(timer.Advance(50, ThinkState::Combat));
    EXPECT_TRUE(timer.Advance(50, ThinkState::Combat));
    EXPECT_FALSE(timer.Advance(99, ThinkState::Combat));
    EXPECT_TRUE(timer.Advance(1, ThinkState::Combat));
}

TEST(ThinkTimer, NearAPlayerThinksEvery500Ms)
{
    ThinkTimer timer;
    timer.Advance(0, ThinkState::NearPlayer);

    EXPECT_FALSE(timer.Advance(499, ThinkState::NearPlayer));
    EXPECT_TRUE(timer.Advance(1, ThinkState::NearPlayer));
}

TEST(ThinkTimer, FarFromAnyPlayerThinksEvery3000Ms)
{
    ThinkTimer timer;
    timer.Advance(0, ThinkState::Far);

    EXPECT_FALSE(timer.Advance(2999, ThinkState::Far));
    EXPECT_TRUE(timer.Advance(1, ThinkState::Far));
}

TEST(ThinkTimer, ThinksAtOnceWhenCombatStartsDuringALongWait)
{
    ThinkTimer timer;
    timer.Advance(0, ThinkState::Far);

    EXPECT_FALSE(timer.Advance(200, ThinkState::Far));
    EXPECT_TRUE(timer.Advance(16, ThinkState::Combat));
}

TEST(ThinkTimer, UsesTheIntervalsOfTheSettings)
{
    ThinkSettings settings;
    settings.combatMs = 250;
    settings.nearPlayerMs = 1000;
    settings.farMs = 5000;
    ThinkTimer timer(settings);
    timer.Advance(0, ThinkState::Far);

    EXPECT_FALSE(timer.Advance(4999, ThinkState::Far));
    EXPECT_TRUE(timer.Advance(1, ThinkState::Far));
    EXPECT_FALSE(timer.Advance(999, ThinkState::NearPlayer));
    EXPECT_TRUE(timer.Advance(1, ThinkState::NearPlayer));
    EXPECT_FALSE(timer.Advance(249, ThinkState::Combat));
    EXPECT_TRUE(timer.Advance(1, ThinkState::Combat));
}

TEST(ThinkState, CombatComesBeforeTheDistanceToPlayers)
{
    EXPECT_EQ(Bots::ThinkStateFor(true, false), ThinkState::Combat);
    EXPECT_EQ(Bots::ThinkStateFor(true, true), ThinkState::Combat);
    EXPECT_EQ(Bots::ThinkStateFor(false, true), ThinkState::NearPlayer);
    EXPECT_EQ(Bots::ThinkStateFor(false, false), ThinkState::Far);
}
