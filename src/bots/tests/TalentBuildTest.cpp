#include "TalentBuild.h"

#include <gtest/gtest.h>

using Bots::BotClass;
using Bots::BotRole;
using Bots::ParseTalentBuild;
using Bots::TalentBuildResult;

TEST(TalentBuild, ReadsABuild)
{
    TalentBuildResult result = ParseTalentBuild(
        "# A Warrior tank\n"
        "build warrior_tank warrior tank\n"
        "pick 1601 1   # Shield Specialization\n"
        "\n"
        "pick 1601 2   # Shield Specialization\n");

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.build.name, "warrior_tank");
    EXPECT_EQ(result.build.botClass, BotClass::Warrior);
    EXPECT_EQ(result.build.role, BotRole::Tank);
    ASSERT_EQ(result.build.picks.size(), 2u);
    EXPECT_EQ(result.build.picks[0].talentId, 1601u);
    EXPECT_EQ(result.build.picks[0].rank, 1u);
    EXPECT_EQ(result.build.picks[0].line, 3u);
    EXPECT_EQ(result.build.picks[1].rank, 2u);
    EXPECT_EQ(result.build.picks[1].line, 5u);
}

namespace
{
    std::string ErrorOf(std::string const& text)
    {
        TalentBuildResult result = ParseTalentBuild(text);
        EXPECT_FALSE(result.ok);
        return result.error;
    }

    std::string Picks(std::size_t count)
    {
        std::string text;
        for (std::size_t i = 0; i < count; ++i)
            text += "pick 1601 1\n";
        return text;
    }
}

TEST(TalentBuild, RejectsBadLines)
{
    EXPECT_EQ(ErrorOf(""), "line 1: The file has no build line.");
    EXPECT_EQ(ErrorOf("pick 1601 1\n"), "line 1: The first line must be 'build <name> <class> <role>'.");
    EXPECT_EQ(ErrorOf("build warrior_tank warrior\n"), "line 1: Write 'build <name> <class> <role>'.");
    EXPECT_EQ(ErrorOf("build Warrior.Tank warrior tank\n"),
        "line 1: The name 'Warrior.Tank' may have only a-z, 0-9, _ and -.");
    EXPECT_EQ(ErrorOf("build x paladin tank\n"), "line 1: Unknown class 'paladin'.");
    EXPECT_EQ(ErrorOf("build x warrior caster\n"), "line 1: Unknown role 'caster'.");
    EXPECT_EQ(ErrorOf("build x mage tank\n"), "line 1: A mage cannot play the role tank.");
    EXPECT_EQ(ErrorOf("build x warrior tank\nbuild y warrior tank\n"), "line 2: The build line comes once.");
    EXPECT_EQ(ErrorOf("build x warrior tank\nitem head 1 1-2 0\n"), "line 2: Unknown line kind 'item'.");
    EXPECT_EQ(ErrorOf("build x warrior tank\npick 1601\n"), "line 2: Write 'pick <talentId> <rank>'.");
    EXPECT_EQ(ErrorOf("build x warrior tank\npick 16x1 1\n"), "line 2: '16x1' is not a talent ID.");
    EXPECT_EQ(ErrorOf("build x warrior tank\npick 1601 0\n"), "line 2: The rank '0' is not 1 to 5.");
    EXPECT_EQ(ErrorOf("build x warrior tank\npick 1601 6\n"), "line 2: The rank '6' is not 1 to 5.");
    EXPECT_EQ(ErrorOf("build x warrior tank\n" + Picks(52)),
        "line 53: A build has at most 51 picks, one for each level from 10 to 60.");
}

TEST(TalentBuild, AcceptsFiftyOnePicks)
{
    EXPECT_TRUE(ParseTalentBuild("build x warrior tank\n" + Picks(51)).ok);
}

namespace
{
    // A few talents of 1.12.1 (see tests/data/talents.tsv).
    Bots::TalentTable SomeTalents()
    {
        Bots::TalentTable table;
        auto add = [&](std::uint32_t id, BotClass botClass, char const* tree, std::uint32_t tier, std::uint32_t maxRank,
            std::uint32_t requiredTalent, std::uint32_t requiredRank)
        {
            table[id] = Bots::TalentInfo{id, botClass, tree, tier, maxRank, requiredTalent, requiredRank};
        };
        add(1601, BotClass::Warrior, "protection", 1, 5, 0, 0); // Shield Specialization
        add(138, BotClass::Warrior, "protection", 1, 5, 0, 0);  // Anticipation
        add(140, BotClass::Warrior, "protection", 2, 5, 0, 0);  // Toughness
        add(142, BotClass::Warrior, "protection", 2, 2, 0, 0);  // Improved Bloodrage
        add(145, BotClass::Warrior, "protection", 3, 3, 1601, 5); // Improved Shield Block
        add(124, BotClass::Warrior, "arms", 1, 3, 0, 0);        // Improved Heroic Strike
        add(37, BotClass::Mage, "frost", 1, 5, 0, 0);           // Improved Frostbolt
        return table;
    }

    Bots::TalentBuild Build(std::vector<std::pair<std::uint32_t, std::uint32_t>> const& picks)
    {
        Bots::TalentBuild build{"warrior_tank", BotClass::Warrior, BotRole::Tank, {}};
        for (std::size_t i = 0; i < picks.size(); ++i)
            build.picks.push_back(Bots::TalentPick{picks[i].first, picks[i].second, i + 2});
        return build;
    }

    std::string RuleError(std::vector<std::pair<std::uint32_t, std::uint32_t>> const& picks)
    {
        return Bots::CheckTalentRules(Build(picks), SomeTalents());
    }

    // Shield Specialization 1 to 5.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> const FiveInTier1 = {
        {1601, 1}, {1601, 2}, {1601, 3}, {1601, 4}, {1601, 5}};

    std::vector<std::pair<std::uint32_t, std::uint32_t>> With(
        std::vector<std::pair<std::uint32_t, std::uint32_t>> picks,
        std::vector<std::pair<std::uint32_t, std::uint32_t>> const& more)
    {
        picks.insert(picks.end(), more.begin(), more.end());
        return picks;
    }
}

TEST(TalentRules, AcceptsABuildThatKeepsTheRules)
{
    EXPECT_EQ(RuleError(With(FiveInTier1,
        {{140, 1}, {140, 2}, {124, 1}, {140, 3}, {140, 4}, {140, 5}, {145, 1}, {145, 2}})), "");
}

TEST(TalentRules, RejectsAnUnknownTalentAndATalentOfAnotherClass)
{
    EXPECT_EQ(RuleError({{9999, 1}}), "line 2: No talent has the ID 9999.");
    EXPECT_EQ(RuleError({{37, 1}}), "line 2: The talent 37 is a mage talent, not a warrior talent.");
}

TEST(TalentRules, RejectsAPickAboveTheMaximumRank)
{
    EXPECT_EQ(RuleError({{124, 1}, {124, 2}, {124, 3}, {124, 4}}),
        "line 5: The talent 124 has at most 3 ranks.");
}

TEST(TalentRules, RejectsARankThatIsNotTheNextRank)
{
    EXPECT_EQ(RuleError({{124, 2}}), "line 2: Write rank 1: the talent 124 has rank 0 before this pick.");
    EXPECT_EQ(RuleError({{124, 1}, {124, 1}}), "line 3: Write rank 2: the talent 124 has rank 1 before this pick.");
}

TEST(TalentRules, RejectsAPickBeforeItsTierHasFivePointsPerTier)
{
    // Toughness is in tier 2: 4 points in protection and 1 in arms are not enough.
    EXPECT_EQ(RuleError({{1601, 1}, {1601, 2}, {1601, 3}, {124, 1}, {138, 1}, {140, 1}}),
        "line 7: The talent 140 is in tier 2, which needs 5 points in the tree protection before it. "
        "The build has 4 there.");
}

TEST(TalentRules, RejectsAPickBeforeItsPrerequisite)
{
    // Improved Shield Block needs Shield Specialization at rank 5.
    EXPECT_EQ(RuleError({{1601, 1}, {1601, 2}, {1601, 3}, {1601, 4}, {138, 1}, {140, 1}, {140, 2},
        {140, 3}, {140, 4}, {140, 5}, {145, 1}}),
        "line 12: The talent 145 needs rank 5 of the talent 1601 first.");
}

TEST(TalentBuild, TheLevelRoutineUsesTheBuildNamedForTheClassAndRole)
{
    EXPECT_EQ(Bots::TalentBuildName(BotClass::Mage, BotRole::Ranged), "mage_ranged");
}

TEST(TalentBuild, TheFileNameIsTheBuildName)
{
    Bots::TalentBuild build{"warrior_tank", BotClass::Warrior, BotRole::Tank, {}};
    EXPECT_EQ(Bots::CheckTalentBuildFileName(build, "warrior_tank"), "");
    EXPECT_EQ(Bots::CheckTalentBuildFileName(build, "tank"), "The file of the build warrior_tank must be warrior_tank.txt.");
}

TEST(TalentPicks, ACharacterAtLevelLHasLMinus9Picks)
{
    EXPECT_EQ(Bots::TalentPicksForLevel(1), 0u);
    EXPECT_EQ(Bots::TalentPicksForLevel(9), 0u);
    EXPECT_EQ(Bots::TalentPicksForLevel(10), 1u);
    EXPECT_EQ(Bots::TalentPicksForLevel(16), 7u);
    EXPECT_EQ(Bots::TalentPicksForLevel(60), 51u);
}

namespace
{
    using Known = std::map<std::uint32_t, std::uint32_t>;

    std::vector<std::pair<std::uint32_t, std::uint32_t>> Learned(Bots::TalentPlan const& plan)
    {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> picks;
        for (Bots::TalentPick const& pick : plan.learn)
            picks.emplace_back(pick.talentId, pick.rank);
        return picks;
    }

    using PickList = std::vector<std::pair<std::uint32_t, std::uint32_t>>;
}

TEST(TalentPicks, ANewLevel16BotLearnsTheFirst7Picks)
{
    Bots::TalentPlan plan = Bots::PlanTalentPicks(Build(With(FiveInTier1, {{140, 1}, {140, 2}, {140, 3}})), 16, {});

    EXPECT_FALSE(plan.reset);
    EXPECT_EQ(Learned(plan), With(FiveInTier1, {{140, 1}, {140, 2}}));
}

TEST(TalentPicks, ALevelUpLearnsOnlyTheNewPick)
{
    Bots::TalentPlan plan = Bots::PlanTalentPicks(Build(With(FiveInTier1, {{140, 1}, {140, 2}, {140, 3}})), 17,
        Known{{1601, 5}, {140, 2}});

    EXPECT_FALSE(plan.reset);
    EXPECT_EQ(Learned(plan), (PickList{{140, 3}}));
}

TEST(TalentPicks, ABotWithItsPicksLearnsNothing)
{
    Bots::TalentPlan plan = Bots::PlanTalentPicks(Build(FiveInTier1), 13, Known{{1601, 4}});

    EXPECT_FALSE(plan.reset);
    EXPECT_TRUE(plan.learn.empty());
}

TEST(TalentPicks, ABotBelowLevel10LearnsNothing)
{
    Bots::TalentPlan plan = Bots::PlanTalentPicks(Build(FiveInTier1), 9, {});

    EXPECT_FALSE(plan.reset);
    EXPECT_TRUE(plan.learn.empty());
}

TEST(TalentPicks, AShortBuildStopsAtItsLastPick)
{
    Bots::TalentPlan plan = Bots::PlanTalentPicks(Build({{1601, 1}, {1601, 2}}), 30, Known{{1601, 1}});

    EXPECT_FALSE(plan.reset);
    EXPECT_EQ(Learned(plan), (PickList{{1601, 2}}));
}

TEST(TalentPicks, ABotWithMorePicksThanLMinus9ResetsFirst)
{
    // After a GM level-down from 16 to 12: 7 picks, 3 wanted.
    Bots::TalentPlan plan = Bots::PlanTalentPicks(Build(With(FiveInTier1, {{140, 1}, {140, 2}})), 12,
        Known{{1601, 5}, {140, 2}});

    EXPECT_TRUE(plan.reset);
    EXPECT_EQ(Learned(plan), (PickList{{1601, 1}, {1601, 2}, {1601, 3}}));
}

TEST(TalentPicks, ABotWithATalentOffTheBuildResetsFirst)
{
    Bots::TalentPlan plan = Bots::PlanTalentPicks(Build(FiveInTier1), 11, Known{{124, 1}});

    EXPECT_TRUE(plan.reset);
    EXPECT_EQ(Learned(plan), (PickList{{1601, 1}, {1601, 2}}));
}
