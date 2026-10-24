// Checks the talent builds in bots/data/talents/ against the talents of
// 1.12.1 (tests/data/talents.tsv, made by bots/tools/talents/export-talents.sh):
// each build keeps the talent rules, and the comment of each pick is the
// talent name.
#include "TalentBuild.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
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

    struct Reference
    {
        Bots::TalentTable talents;
        std::map<std::uint32_t, std::string> names;
    };

    Reference ReadReference()
    {
        Reference reference;
        std::istringstream lines(ReadFile(std::string(BOT_TEST_DATA_DIR) + "/talents.tsv"));
        std::string line;
        while (std::getline(lines, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            std::vector<std::string> fields;
            std::istringstream split(line);
            std::string field;
            while (std::getline(split, field, '\t'))
                fields.push_back(Trim(field));
            if (fields.size() != 9)
                continue;
            Bots::BotClass botClass;
            if (!Bots::ParseClassName(fields[1], botClass))
                continue; // The Paladin, which the bots do not play.
            std::uint32_t const id = static_cast<std::uint32_t>(std::stoul(fields[0]));
            // fields[4] is the column, which the rules do not need.
            reference.talents[id] = Bots::TalentInfo{id, botClass, fields[2],
                static_cast<std::uint32_t>(std::stoul(fields[3])), static_cast<std::uint32_t>(std::stoul(fields[5])),
                static_cast<std::uint32_t>(std::stoul(fields[6])), static_cast<std::uint32_t>(std::stoul(fields[7]))};
            reference.names[id] = fields[8];
        }
        return reference;
    }

    // The comment of each line with words, in file order.
    std::vector<std::string> LineComments(std::string const& text)
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

    std::vector<std::filesystem::path> BuildFiles()
    {
        std::vector<std::filesystem::path> files;
        std::filesystem::path const folder = std::filesystem::path(BOT_DATA_DIR) / "talents";
        if (std::filesystem::is_directory(folder))
            for (auto const& entry : std::filesystem::directory_iterator(folder))
                if (entry.path().extension() == ".txt")
                    files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        return files;
    }
}

TEST(TalentData, TheReferenceTableHasTheTalentsOf1121)
{
    Reference const reference = ReadReference();

    EXPECT_GT(reference.talents.size(), 350u);
    Bots::TalentInfo const& block = reference.talents.at(145);
    EXPECT_EQ(reference.names.at(145), "Improved Shield Block");
    EXPECT_EQ(block.botClass, Bots::BotClass::Warrior);
    EXPECT_EQ(block.tree, "protection");
    EXPECT_EQ(block.tier, 3u);
    EXPECT_EQ(block.maxRank, 3u);
    EXPECT_EQ(block.requiredTalent, 1601u);
    EXPECT_EQ(block.requiredRank, 5u);
    EXPECT_EQ(reference.talents.at(37).tree, "frost"); // Improved Frostbolt
}

TEST(TalentData, RejectsAFrostBuildThatSkipsATier)
{
    // Improved Frostbolt 1 to 4, then Piercing Ice of tier 3.
    Bots::TalentBuildResult result = Bots::ParseTalentBuild(
        "build mage_ranged mage ranged\n"
        "pick 37 1\npick 37 2\npick 37 3\npick 37 4\npick 61 1\n");

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(Bots::CheckTalentRules(result.build, ReadReference().talents),
        "line 6: The talent 61 is in tier 3, which needs 10 points in the tree frost before it. The build has 4 there.");
}

TEST(TalentData, EachBuildFileKeepsTheTalentRules)
{
    Reference const reference = ReadReference();
    for (std::filesystem::path const& path : BuildFiles())
    {
        SCOPED_TRACE(path.string());
        std::string const text = ReadFile(path.string());
        Bots::TalentBuildResult result = Bots::ParseTalentBuild(text);
        ASSERT_TRUE(result.ok) << result.error;
        EXPECT_EQ(Bots::CheckTalentBuildFileName(result.build, path.stem().string()), "");
        EXPECT_EQ(Bots::CheckTalentRules(result.build, reference.talents), "");

        // The first comment line is the build line; its comment is free.
        std::vector<std::string> const comments = LineComments(text);
        ASSERT_EQ(comments.size(), result.build.picks.size() + 1);
        for (std::size_t i = 0; i < result.build.picks.size(); ++i)
        {
            std::uint32_t const id = result.build.picks[i].talentId;
            auto const name = reference.names.find(id);
            if (name != reference.names.end())
                EXPECT_EQ(comments[i + 1], name->second) << "the comment of the pick on line " << result.build.picks[i].line;
        }
    }
}
