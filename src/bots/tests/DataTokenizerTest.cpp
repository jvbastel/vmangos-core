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
