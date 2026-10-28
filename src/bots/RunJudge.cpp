#include "RunJudge.h"

#include "FightSpot.h"

#include <algorithm>
#include <cstdio>

namespace Bots
{
    RunJudge::RunJudge(std::vector<std::uint32_t> bosses, std::int64_t startMs)
        : m_bosses(std::move(bosses)), m_startMs(startMs), m_progressMs(startMs)
    {
    }

    void RunJudge::NoteBossKill(std::uint32_t entry, std::int64_t nowMs)
    {
        if (std::find(m_bosses.begin(), m_bosses.end(), entry) != m_bosses.end())
            m_killed.insert(entry);
        NoteProgress(nowMs);
    }

    void RunJudge::NoteWipe()
    {
        ++m_wipes;
    }

    void RunJudge::NoteProgress(std::int64_t nowMs)
    {
        m_progressMs = std::max(m_progressMs, nowMs);
    }

    RunVerdict RunJudge::Judge(std::int64_t nowMs) const
    {
        if (!m_bosses.empty() && m_killed.size() == m_bosses.size())
            return {RunVerdictKind::Passed, ""};
        if (m_wipes >= FailingWipe)
            return {RunVerdictKind::Failed, "the second wipe"};
        if (nowMs - m_startMs >= RunLimitMs)
            return {RunVerdictKind::Failed, "the run did not end in " + std::to_string(RunLimitMs / 60000) + " min"};
        if (nowMs - m_progressMs >= NoProgressMs)
            return {RunVerdictKind::Failed, "no progress for " + std::to_string(NoProgressMs / 60000) +
                " min: no kill, no damage dealt, and no movement along the route"};
        return {RunVerdictKind::Running, ""};
    }

    RunVerdict RunJudge::AtScriptEnd() const
    {
        if (m_killed.size() == m_bosses.size())
            return {RunVerdictKind::Passed, ""};
        return {RunVerdictKind::Failed, "the route ended with " + std::to_string(m_killed.size()) + " of " +
            std::to_string(m_bosses.size()) + " bosses dead"};
    }
}

namespace Bots
{
    namespace
    {
        std::string Place(Position const& at)
        {
            char text[64];
            std::snprintf(text, sizeof(text), "%.1f %.1f %.1f", at.x, at.y, at.z);
            return text;
        }

        bool Has(std::vector<Guid> const& guids, Guid guid)
        {
            return std::find(guids.begin(), guids.end(), guid) != guids.end();
        }
    }

    PullChoices FindPullChoices(BotView const& tank, std::vector<Position> const& trail, Guid target,
        std::vector<RoutePackAtPull> const& packs)
    {
        PullOptions const options = FindPullOptions(tank, trail, target);
        // The route packs of the enemies of a pack of the search.
        auto const routePacksOf = [&packs](std::vector<NearEnemy> const& enemies) {
            std::set<std::string> names;
            for (NearEnemy const& enemy : enemies)
                for (RoutePackAtPull const& pack : packs)
                    if (Has(pack.enemies, enemy.guid))
                        names.insert(pack.name);
            return names;
        };
        PullChoices result;
        result.withTarget = routePacksOf(options.split.pack);
        result.searched = result.withTarget;
        std::vector<std::set<std::string>> others;
        for (std::vector<NearEnemy> const& pack : options.split.others)
        {
            others.push_back(routePacksOf(pack));
            result.searched.insert(others.back().begin(), others.back().end());
        }
        for (PullOption const& option : options.options)
        {
            PullChoice choice;
            switch (option.way)
            {
                case PullWay::FightSpot:
                    choice.way = "a fight spot at " + Place(option.spot) + " on the cleared way";
                    break;
                case PullWay::LineOfSight:
                    choice.way = "a line-of-sight pull at " + Place(option.spot);
                    break;
                case PullWay::SpawnArea:
                    choice.way = "the area round the caster's spawn";
                    break;
            }
            choice.brings = result.withTarget;
            for (std::size_t pack : option.packs)
                choice.brings.insert(others[pack].begin(), others[pack].end());
            result.choices.push_back(choice);
        }
        return result;
    }

    ExtraPackMark MarkExtraPack(PullChoices const& choices, std::string const& pack, std::size_t got)
    {
        if (choices.withTarget.count(pack))
            return {false, "it is one pack with the target: within " + std::to_string(int(PackLinkYards)) +
                " yd of it"};
        if (!choices.searched.count(pack))
            return {true, "it was out of the fight spot search at the pull: the fight went to it"};
        // The pull that keeps the pack out with the fewest extra packs, the
        // first of a tie.
        PullChoice const* best = nullptr;
        for (PullChoice const& choice : choices.choices)
            if (!choice.brings.count(pack) && choice.brings.size() <= got
                && (!best || choice.brings.size() < best->brings.size()))
                best = &choice;
        if (!best)
            return {false, "no fight spot, line-of-sight pull or spawn area keeps it out with at most " +
                std::to_string(got) + (got == 1 ? " extra pack" : " extra packs")};
        std::string reason = best->way + " keeps it out";
        if (!best->brings.empty())
            reason += ", with " + std::to_string(best->brings.size()) + " other extra pack" +
                (best->brings.size() == 1 ? "" : "s");
        return {true, reason};
    }

    ExtraPackMark JudgeExtraPack(bool pullJudged, ExtraPackMark const* atPull, ExtraPackCause const& cause)
    {
        if (cause.scripted)
            return {false, "the script brought it (join)"};
        if (!cause.fleeing.empty())
            return {false, "a fleeing enemy brought it: " + cause.fleeing};
        if (cause.patrol)
            return {false, "a patrol brought it"};
        if (!pullJudged)
            return {false, "not judged: the run saw no tank and target at the pull"};
        if (!atPull)
            return {true, "it was out of the fight spot search at the pull: the fight went to it"};
        return *atPull;
    }

    std::string PullFailure(RunPull const& pull)
    {
        for (RunExtraPack const& extra : pull.extraPacks)
        {
            if (!extra.warned)
                return "extra pack " + extra.pack + " joined with no warn";
            if (extra.avoidable)
                return "extra pack " + extra.pack + " was avoidable: " + extra.reason;
        }
        return "";
    }

    std::size_t FailedPulls(std::vector<RunPull> const& pulls)
    {
        std::size_t failed = 0;
        for (RunPull const& pull : pulls)
            if (!PullFailure(pull).empty())
                ++failed;
        return failed;
    }
}
