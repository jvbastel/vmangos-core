#include "DataTokenizer.h"

#include <gtest/gtest.h>

using Bots::DataLine;
using Bots::TokenizeLines;
using Words = std::vector<std::string>;

TEST(DataTokenizer, SplitsEachLineIntoWords)
{
    std::vector<DataLine> lines = TokenizeLines(
        "build frost_mage mage dps\n"
        "pick 61 5\n");

    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0].words, (Words{"build", "frost_mage", "mage", "dps"}));
    EXPECT_EQ(lines[1].words, (Words{"pick", "61", "5"}));
}

TEST(DataTokenizer, DropsCommentsAndLinesWithoutWords)
{
    std::vector<DataLine> lines = TokenizeLines(
        "# Frost mage gear\n"
        "\n"
        "item head 10041 54-60 1 812 # Dreamweave Circlet\n"
        "   \t \n"
        "   # an indented comment\n");

    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].number, 3u);
    EXPECT_EQ(lines[0].words, (Words{"item", "head", "10041", "54-60", "1", "812"}));
}

TEST(DataTokenizer, CommentMarkerEndsAWordToo)
{
    std::vector<DataLine> lines = TokenizeLines("ban 12345# old note\n");

    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].words, (Words{"ban", "12345"}));
}

TEST(DataTokenizer, ReadsWindowsLineEndsAndALastLineWithoutLineEnd)
{
    std::vector<DataLine> lines = TokenizeLines("teleport\r\nmove p1 # door\r\nwait combat_end");

    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0].words, (Words{"teleport"}));
    EXPECT_EQ(lines[1].words, (Words{"move", "p1"}));
    EXPECT_EQ(lines[2].number, 3u);
    EXPECT_EQ(lines[2].words, (Words{"wait", "combat_end"}));
}
