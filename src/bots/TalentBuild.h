#ifndef WOWAI_BOTS_TALENT_BUILD_H
#define WOWAI_BOTS_TALENT_BUILD_H

#include "BotRecipe.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Talent builds: the order in which a bot of one class and role spends its
// talent points to level 60. See "Talent build" in the WOWAI glossary and
// step 4 of the version 1 build spec.
//
// FILE FORMAT
//
// One build per file. The game layer reads every `*.txt` file of the folder
// `<Bots.DataDir>/talents/` at startup. In the fork, that folder is
// `bots/data/talents/`. The shared tokenizer of DataTokenizer.h splits the
// lines: words are split by spaces and tabs, `#` starts a comment to the end
// of the line, and blank lines are dropped.
//
//   build <name> <class> <role>
//   pick <talentId> <rank>      # Talent name
//
// - `build` is the first line, once. The name has only the characters a-z,
//   0-9, `_`, and `-`, and it is the file name without `.txt`. The class and
//   role are the words of `.wowai bot create`: warrior, priest, rogue,
//   warlock, mage, hunter, shaman, druid; tank, healer, melee, ranged. The
//   class must be able to play the role.
// - The level routine gives a bot the build named `<class>_<role>`, for
//   example `warrior_tank`. A build with another name is kept for later (a
//   class can have several builds per role) and is checked the same way.
// - `pick`: one line per talent point, in the order of the levels. The first
//   pick is the point of level 10, the second the point of level 11, and so
//   on: a character at level L has the first L minus 9 picks. So a build has
//   at most 51 picks. `<talentId>` is the ID of the talent in Talent.dbc (not
//   a spell ID). `<rank>` is the rank that the talent has after this point,
//   counted from 1: the first point in a talent is rank 1, the next point in
//   it is rank 2, and so on. The comment has the talent name as the talent
//   window shows it, which is the name of its rank 1 spell.
// - The talent rules, checked at load and by a GoogleTest test
//   (CheckTalentRules):
//   - Each talent is a talent of the class of the build.
//   - The ranks of a talent come one at a time from 1: rank R comes after
//     rank R minus 1, and never above the maximum rank of the talent.
//   - 5 points per tier before the next tier: a talent in tier T (counted
//     from 1, the top row of the tree) needs 5 times (T minus 1) points in
//     its tree before the pick.
//   - A talent with a prerequisite (the arrow in the talent window) needs
//     the prerequisite talent at its full required rank first.
//
// Example (a Warrior tank, from the talent IDs of 1.12.1):
//
//   build warrior_tank warrior tank
//   pick 1601 1    # Shield Specialization
//   pick 1601 2    # Shield Specialization
//   pick 1601 3    # Shield Specialization
//   pick 1601 4    # Shield Specialization
//   pick 1601 5    # Shield Specialization
//   pick 140 1     # Toughness (tier 2: 5 points in the tree before it)
//   pick 140 2     # Toughness
//   pick 140 3     # Toughness
//   pick 140 4     # Toughness
//   pick 140 5     # Toughness
//   pick 145 1     # Improved Shield Block (tier 3, and it needs
//                  # Shield Specialization at rank 5)
//
// The table of the talents of 1.12.1, with the IDs, trees, tiers, maximum
// ranks, prerequisites, and names, is `src/bots/tests/data/talents.tsv`, made
// by `bots/tools/talents/export-talents.sh`.
namespace Bots
{
    // The most picks of a build: one per level from 10 to 60.
    std::uint32_t const MaxTalentPicks = 51;

    // One talent of Talent.dbc, as plain data. The game layer makes the table
    // from its DBC stores, the tests from tests/data/talents.tsv.
    struct TalentInfo
    {
        std::uint32_t talentId;
        BotClass botClass;
        std::string tree;            // Any word that is the same for each talent of one tree.
        std::uint32_t tier;          // 1 for the top row.
        std::uint32_t maxRank;       // 1 to 5.
        std::uint32_t requiredTalent; // 0 when none.
        std::uint32_t requiredRank;  // The rank of the required talent, from 1.
    };

    using TalentTable = std::map<std::uint32_t /*talentId*/, TalentInfo>;

    struct TalentPick
    {
        std::uint32_t talentId;
        std::uint32_t rank;  // From 1.
        std::size_t line;    // The line in the file, for messages.
    };

    struct TalentBuild
    {
        std::string name;
        BotClass botClass;
        BotRole role;
        std::vector<TalentPick> picks;
    };

    struct TalentBuildResult
    {
        bool ok;
        TalentBuild build;  // Only when ok.
        std::string error;  // "line N: ..." when not ok.
    };

    // Reads the text of one build file. It checks the form of the lines; the
    // talent rules need the talent table, see CheckTalentRules.
    TalentBuildResult ParseTalentBuild(std::string const& text);

    // Empty when the build keeps the talent rules, else "line N: ..." for
    // the first pick that breaks one.
    std::string CheckTalentRules(TalentBuild const& build, TalentTable const& talents);

    // Empty when the file name without `.txt` fits the build, else a short
    // line for the log.
    std::string CheckTalentBuildFileName(TalentBuild const& build, std::string const& fileStem);

    // The name of the build that the level routine gives to a class and role,
    // for example "warrior_tank".
    std::string TalentBuildName(BotClass botClass, BotRole role);

    // The number of picks that a character of a level has: L minus 9, from 0.
    std::uint32_t TalentPicksForLevel(std::uint32_t level);

    // What the level routine does with the talents of a bot.
    struct TalentPlan
    {
        // Reset all talents first, because the bot has a talent rank that
        // the first picks for its level do not have: more picks than L minus
        // 9 (for example after a GM level-down), or a talent off the build.
        bool reset;
        // The picks to learn, in order, after the reset if any.
        std::vector<TalentPick> learn;
    };

    // `known` is the rank (from 1) of each talent that the bot has now. After
    // the plan, the bot has exactly the first picks for its level, or all the
    // picks of a shorter build.
    TalentPlan PlanTalentPicks(TalentBuild const& build, std::uint32_t level,
        std::map<std::uint32_t /*talentId*/, std::uint32_t /*rank*/> const& known);
}

#endif
