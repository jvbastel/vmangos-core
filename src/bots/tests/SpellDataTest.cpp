// Checks the spell files of the version 1 classes in bots/data/spells/
// against the learnable spells of the 1.12.1 world database
// (tests/data/learnable-spells.tsv, made by
// bots/tools/spells/export-learnable-spells.sh).
#include "SpellFile.h"

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <sstream>

namespace
{
    std::string ReadFile(std::string const& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }

    std::string Trim(std::string const& text)
    {
        std::size_t const first = text.find_first_not_of(" \t\r");
        if (first == std::string::npos)
            return "";
        return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
    }

    // Spell ID -> the name as the spell files write it: "Firebolt (Rank 2)".
    std::map<std::uint32_t, std::string> LearnableSpells()
    {
        std::map<std::uint32_t, std::string> spells;
        std::istringstream lines(ReadFile(std::string(BOT_TEST_DATA_DIR) + "/learnable-spells.tsv"));
        std::string line;
        while (std::getline(lines, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream fields(line);
            std::string id, name, subtext;
            std::getline(fields, id, '\t');
            std::getline(fields, name, '\t');
            std::getline(fields, subtext);
            subtext = Trim(subtext);
            if (subtext.compare(0, 5, "Rank ") == 0)
                name += " (" + subtext + ")";
            spells[static_cast<std::uint32_t>(std::stoul(id))] = name;
        }
        return spells;
    }

    // The comment of each line with a spell, in file order.
    std::vector<std::string> SpellComments(std::string const& text)
    {
        std::vector<std::string> comments;
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line))
        {
            std::size_t const marker = line.find('#');
            if (Trim(line.substr(0, marker)).empty())
                continue;
            comments.push_back(marker == std::string::npos ? "" : Trim(line.substr(marker + 1)));
        }
        return comments;
    }

    char const* const Version1Classes[] = {"warrior", "priest", "rogue", "warlock", "mage"};
}

TEST(SpellData, TheReferenceListHasTheLearnableSpells)
{
    std::map<std::uint32_t, std::string> spells = LearnableSpells();

    EXPECT_GT(spells.size(), 4000u);
    EXPECT_EQ(spells[697], "Summon Voidwalker");
    EXPECT_EQ(spells[7799], "Firebolt (Rank 2)");
}

TEST(SpellData, EachSpellOfEachClassFileExists)
{
    std::map<std::uint32_t, std::string> const learnable = LearnableSpells();
    for (char const* botClass : Version1Classes)
    {
        SCOPED_TRACE(botClass);
        std::string const text = ReadFile(std::string(BOT_DATA_DIR) + "/spells/" + botClass + ".txt");
        ASSERT_FALSE(text.empty()) << "no spell file in " << BOT_DATA_DIR;

        Bots::SpellFileResult result = Bots::ParseSpellFile(text);
        ASSERT_TRUE(result.ok) << result.error;
        std::vector<std::string> const comments = SpellComments(text);
        ASSERT_EQ(comments.size(), result.spells.size());
        for (std::size_t i = 0; i < result.spells.size(); ++i)
        {
            std::uint32_t const id = result.spells[i].spellId;
            auto const found = learnable.find(id);
            ASSERT_NE(found, learnable.end()) << "spell " << id << " is no learnable spell";
            EXPECT_EQ(comments[i], found->second) << "the comment of spell " << id;
        }
    }
}

TEST(SpellData, TheWarlockFileHasTheSpellsOfTheImp)
{
    Bots::SpellFileResult result = Bots::ParseSpellFile(ReadFile(std::string(BOT_DATA_DIR) + "/spells/warlock.txt"));

    ASSERT_TRUE(result.ok) << result.error;
    // A level 16 Imp: Firebolt 1 and 2, Blood Pact 1 and 2, Fire Shield 1, Phase Shift.
    EXPECT_EQ(Bots::SpellsUpToLevel(result.spells, Bots::SpellKind::Pet, 16),
        (std::vector<std::uint32_t>{3110, 6307, 7799, 4511, 7804, 2947}));
    EXPECT_EQ(Bots::SpellsUpToLevel(result.spells, Bots::SpellKind::Quest, 16),
        (std::vector<std::uint32_t>{688, 697}));
}
