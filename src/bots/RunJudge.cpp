#include "RunJudge.h"

#include <algorithm>

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
