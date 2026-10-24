#include "TalentBuild.h"
#include "DataTokenizer.h"

#include <algorithm>
#include <cstdlib>

namespace Bots
{
    namespace
    {
        // A whole number above 0 with digits only.
        bool ParsePositive(std::string const& word, std::uint32_t& value)
        {
            if (word.empty() || word.size() > 9)
                return false;
            for (char c : word)
                if (c < '0' || c > '9')
                    return false;
            value = static_cast<std::uint32_t>(std::strtoul(word.c_str(), nullptr, 10));
            return value > 0;
        }

        bool IsValidName(std::string const& name)
        {
            if (name.empty())
                return false;
            for (char c : name)
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
                    return false;
            return true;
        }

        std::string Article(char const* word)
        {
            return std::string("aeiou").find(word[0]) != std::string::npos ? "An " : "A ";
        }

        TalentBuildResult Fail(std::size_t line, std::string const& error)
        {
            return TalentBuildResult{false, {}, "line " + std::to_string(line) + ": " + error};
        }

        std::uint32_t const MaxRank = 5; // MAX_TALENT_RANK of the core.
    }

    TalentBuildResult ParseTalentBuild(std::string const& text)
    {
        std::vector<DataLine> const lines = TokenizeLines(text);
        if (lines.empty())
            return Fail(1, "The file has no build line.");

        DataLine const& head = lines[0];
        if (head.words[0] != "build")
            return Fail(head.number, "The first line must be 'build <name> <class> <role>'.");
        if (head.words.size() != 4)
            return Fail(head.number, "Write 'build <name> <class> <role>'.");
        TalentBuild build;
        build.name = head.words[1];
        if (!IsValidName(build.name))
            return Fail(head.number, "The name '" + build.name + "' may have only a-z, 0-9, _ and -.");
        if (!ParseClassName(head.words[2], build.botClass))
            return Fail(head.number, "Unknown class '" + head.words[2] + "'.");
        if (!ParseRoleName(head.words[3], build.role))
            return Fail(head.number, "Unknown role '" + head.words[3] + "'.");
        if (!ClassCanPlayRole(build.botClass, build.role))
            return Fail(head.number, Article(ClassName(build.botClass)) + ClassName(build.botClass) +
                " cannot play the role " + RoleName(build.role) + ".");

        for (std::size_t i = 1; i < lines.size(); ++i)
        {
            DataLine const& line = lines[i];
            std::string const& kind = line.words[0];
            if (kind == "build")
                return Fail(line.number, "The build line comes once.");
            if (kind != "pick")
                return Fail(line.number, "Unknown line kind '" + kind + "'.");
            if (line.words.size() != 3)
                return Fail(line.number, "Write 'pick <talentId> <rank>'.");
            TalentPick pick{0, 0, line.number};
            if (!ParsePositive(line.words[1], pick.talentId))
                return Fail(line.number, "'" + line.words[1] + "' is not a talent ID.");
            if (!ParsePositive(line.words[2], pick.rank) || pick.rank > MaxRank)
                return Fail(line.number, "The rank '" + line.words[2] + "' is not 1 to 5.");
            if (build.picks.size() == MaxTalentPicks)
                return Fail(line.number, "A build has at most 51 picks, one for each level from 10 to 60.");
            build.picks.push_back(pick);
        }
        return TalentBuildResult{true, build, ""};
    }
}

namespace Bots
{
    std::string CheckTalentRules(TalentBuild const& build, TalentTable const& talents)
    {
        std::map<std::uint32_t, std::uint32_t> ranks;    // talentId -> rank before the pick
        std::map<std::string, std::uint32_t> treePoints; // tree -> points before the pick
        for (TalentPick const& pick : build.picks)
        {
            std::string const where = "line " + std::to_string(pick.line) + ": ";
            std::string const talent = "The talent " + std::to_string(pick.talentId);
            auto const found = talents.find(pick.talentId);
            if (found == talents.end())
                return where + "No talent has the ID " + std::to_string(pick.talentId) + ".";
            TalentInfo const& info = found->second;
            if (info.botClass != build.botClass)
                return where + talent + " is a " + ClassName(info.botClass) + " talent, not a " +
                    ClassName(build.botClass) + " talent.";
            if (pick.rank > info.maxRank)
                return where + talent + " has at most " + std::to_string(info.maxRank) +
                    (info.maxRank == 1 ? " rank." : " ranks.");
            std::uint32_t& rank = ranks[pick.talentId];
            if (pick.rank != rank + 1)
                return where + "Write rank " + std::to_string(rank + 1) + ": the talent " +
                    std::to_string(pick.talentId) + " has rank " + std::to_string(rank) + " before this pick.";
            std::uint32_t& points = treePoints[info.tree];
            std::uint32_t const needed = 5 * (info.tier - 1);
            if (points < needed)
                return where + talent + " is in tier " + std::to_string(info.tier) + ", which needs " +
                    std::to_string(needed) + " points in the tree " + info.tree + " before it. The build has " +
                    std::to_string(points) + " there.";
            if (info.requiredTalent && ranks[info.requiredTalent] < info.requiredRank)
                return where + talent + " needs rank " + std::to_string(info.requiredRank) + " of the talent " +
                    std::to_string(info.requiredTalent) + " first.";
            ++rank;
            ++points;
        }
        return "";
    }
}

namespace Bots
{
    std::string CheckTalentBuildFileName(TalentBuild const& build, std::string const& fileStem)
    {
        if (build.name != fileStem)
            return "The file of the build " + build.name + " must be " + build.name + ".txt.";
        return "";
    }

    std::string TalentBuildName(BotClass botClass, BotRole role)
    {
        return std::string(ClassName(botClass)) + "_" + RoleName(role);
    }

    std::uint32_t TalentPicksForLevel(std::uint32_t level)
    {
        return level < 10 ? 0 : level - 9;
    }

    TalentPlan PlanTalentPicks(TalentBuild const& build, std::uint32_t level,
        std::map<std::uint32_t, std::uint32_t> const& known)
    {
        std::size_t const count = std::min<std::size_t>(TalentPicksForLevel(level), build.picks.size());
        std::map<std::uint32_t, std::uint32_t> wanted;
        for (std::size_t i = 0; i < count; ++i)
            wanted[build.picks[i].talentId] = build.picks[i].rank;

        // A rank above the wanted rank cannot be unlearned alone.
        TalentPlan plan{false, {}};
        for (auto const& talent : known)
        {
            auto const want = wanted.find(talent.first);
            if (talent.second > (want == wanted.end() ? 0 : want->second))
                plan.reset = true;
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            TalentPick const& pick = build.picks[i];
            auto const have = known.find(pick.talentId);
            if (plan.reset || have == known.end() || have->second < pick.rank)
                plan.learn.push_back(pick);
        }
        return plan;
    }
}
