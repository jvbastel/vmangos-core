#include "CastResult.h"

#include <gtest/gtest.h>

using Bots::CastResultText;

// The 1.12.1 values come from the SpellCastResult enum of
// src/game/Spells/SpellDefines.h. BotAdapter.cpp checks the whole name list
// against that enum when the core compiles.
TEST(CastResult, NamesAFailure)
{
    EXPECT_EQ("result 62 (SPELL_FAILED_NOT_STANDING)", CastResultText(62));
    EXPECT_EQ("result 43 (SPELL_FAILED_LOWLEVEL)", CastResultText(43));
}

TEST(CastResult, NamesTheFirstAndLastFailures)
{
    EXPECT_EQ("result 0 (SPELL_FAILED_AFFECTING_COMBAT)", CastResultText(0));
    EXPECT_EQ("result 145 (SPELL_FAILED_UNKNOWN)", CastResultText(145));
}

TEST(CastResult, NamesSuccess)
{
    EXPECT_EQ("result 255 (SPELL_CAST_OK)", CastResultText(255));
}

TEST(CastResult, KeepsTheNumberOfAnUnknownResult)
{
    EXPECT_EQ("result 200 (unknown)", CastResultText(200));
}
