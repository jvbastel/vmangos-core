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

    ExtraPackMark MarkExtraPack(BotView const& tank, std::vector<Position> const& trail, Guid target,
        std::vector<Guid> const& pack)
    {
        std::vector<NearEnemy> const enemies = tank.GetEnemiesNear(FightSpotEnemyYards);
        TargetPack const split = SplitTargetPack(enemies, target, tank.GetPosition(target));
        std::vector<NearEnemy> kept = split.pack;
        bool seen = false;
        for (NearEnemy const& enemy : split.pack)
            if (Has(pack, enemy.guid))
                return {false, "it is one pack with the target: within " + std::to_string(int(PackLinkYards)) +
                    " yd of it"};
        for (NearEnemy const& enemy : enemies)
            if (Has(pack, enemy.guid))
            {
                kept.push_back(enemy);
                seen = true;
            }
        if (!seen)
            return {true, "it was out of the fight spot search at the pull: the fight went to it"};

        FightSpot const spot = FindFightSpot(tank, trail, target, kept);
        if (spot.clean && spot.lineOfSight)
            return {true, "a line-of-sight pull at " + Place(spot.spot) + " keeps it out"};
        if (spot.clean)
            return {true, "a fight spot at " + Place(spot.spot) + " on the cleared way keeps it out"};
        if (IsCasterPack(tank, split.pack))
            return {false, "no line-of-sight pull, and the caster's spawn area or each fight spot pulls it"};
        return {false, "no fight spot on the cleared way keeps it out"};
    }

    ExtraPackMark JudgeExtraPack(ExtraPackMark const* atPull, ExtraPackCause const& cause)
    {
        if (!cause.fleeing.empty())
            return {false, "a fleeing enemy brought it: " + cause.fleeing};
        if (cause.patrol)
            return {false, "a patrol brought it"};
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

    RunVerdict JudgePulls(std::vector<RunPull> const& pulls)
    {
        for (std::size_t i = 0; i < pulls.size(); ++i)
        {
            std::string const failure = PullFailure(pulls[i]);
            if (!failure.empty())
                return {RunVerdictKind::Failed, "pull " + std::to_string(i + 1) +
                    (pulls[i].pack.empty() ? "" : " (" + pulls[i].pack + ")") + " failed: " + failure};
        }
        return {RunVerdictKind::Passed, ""};
    }
}
