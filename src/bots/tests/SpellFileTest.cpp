#include "SpellFile.h"

#include <gtest/gtest.h>

using Bots::ClassSpell;
using Bots::ParseSpellFile;
using Bots::SpellFileResult;
using Bots::SpellKind;
using Bots::SpellsUpToLevel;

TEST(SpellFile, ReadsQuestAndPetSpellLines)
{
    SpellFileResult result = ParseSpellFile(
        "# Warlock\n"
        "quest-spell 10 697  # Summon Voidwalker\n"
        "pet-spell 8 7799    # Firebolt (Rank 2)\n");

    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.spells.size(), 2u);
    EXPECT_EQ(result.spells[0].kind, SpellKind::Quest);
    EXPECT_EQ(result.spells[0].level, 10u);
    EXPECT_EQ(result.spells[0].spellId, 697u);
    EXPECT_EQ(result.spells[1].kind, SpellKind::Pet);
    EXPECT_EQ(result.spells[1].level, 8u);
    EXPECT_EQ(result.spells[1].spellId, 7799u);
}

TEST(SpellFile, RefusesAnUnknownLineKind)
{
    SpellFileResult result = ParseSpellFile("quest-spell 10 697\ntrainer-spell 4 100\n");

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "Line 2: unknown line kind 'trainer-spell'.");
}

TEST(SpellFile, RefusesALineWithoutLevelAndSpell)
{
    EXPECT_EQ(ParseSpellFile("quest-spell 10\n").error, "Line 1: write 'quest-spell <level> <spellId>'.");
    EXPECT_EQ(ParseSpellFile("pet-spell 4 6307 1\n").error, "Line 1: write 'pet-spell <level> <spellId>'.");
}

TEST(SpellFile, RefusesANumberThatIsNoNumber)
{
    EXPECT_EQ(ParseSpellFile("quest-spell ten 697\n").error, "Line 1: 'ten' is not a positive number.");
    EXPECT_EQ(ParseSpellFile("pet-spell 4 0\n").error, "Line 1: '0' is not a positive number.");
    EXPECT_EQ(ParseSpellFile("pet-spell 4 -3\n").error, "Line 1: '-3' is not a positive number.");
    EXPECT_EQ(ParseSpellFile("pet-spell 99999999999 3\n").error, "Line 1: '99999999999' is not a positive number.");
}

TEST(SpellFile, AnEmptyFileHasNoSpells)
{
    SpellFileResult result = ParseSpellFile("# The Mage has no quest spells.\n");

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.spells.empty());
}

TEST(SpellsUpToLevel, KeepsTheSpellsOfOneKindUpToTheLevel)
{
    std::vector<ClassSpell> spells = {
        {SpellKind::Quest, 10, 71},
        {SpellKind::Pet, 4, 6307},
        {SpellKind::Quest, 16, 1000},
        {SpellKind::Quest, 17, 2000},
    };

    EXPECT_EQ(SpellsUpToLevel(spells, SpellKind::Quest, 16), (std::vector<std::uint32_t>{71, 1000}));
    EXPECT_EQ(SpellsUpToLevel(spells, SpellKind::Pet, 16), (std::vector<std::uint32_t>{6307}));
    EXPECT_TRUE(SpellsUpToLevel(spells, SpellKind::Quest, 9).empty());
}

TEST(SpellsUpToLevel, GivesLowerLevelsFirstSoThatLowerRanksComeFirst)
{
    std::vector<ClassSpell> spells = {
        {SpellKind::Pet, 14, 7804}, // Blood Pact (Rank 2)
        {SpellKind::Pet, 4, 6307},  // Blood Pact (Rank 1)
        {SpellKind::Pet, 8, 7799},
        {SpellKind::Pet, 4, 3110},
    };

    EXPECT_EQ(SpellsUpToLevel(spells, SpellKind::Pet, 60), (std::vector<std::uint32_t>{6307, 3110, 7799, 7804}));
}
