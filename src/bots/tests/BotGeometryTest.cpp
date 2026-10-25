#include "BotGeometry.h"

#include <gtest/gtest.h>

// The distance on the map, without the height.
TEST(Geometry, Distance2dLeavesOutTheHeight)
{
    EXPECT_FLOAT_EQ(Bots::Distance2d({1.0f, 2.0f, 100.0f}, {4.0f, 6.0f, -50.0f}), 5.0f);
}

TEST(Geometry, Distance2dIsTheSameBothWays)
{
    EXPECT_FLOAT_EQ(Bots::Distance2d({-3.0f, 0.0f, 0.0f}, {0.0f, 4.0f, 0.0f}), 5.0f);
    EXPECT_FLOAT_EQ(Bots::Distance2d({0.0f, 4.0f, 0.0f}, {-3.0f, 0.0f, 0.0f}), 5.0f);
}
