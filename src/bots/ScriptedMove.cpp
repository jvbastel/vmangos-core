#include "ScriptedMove.h"

#include "ScriptRunner.h"

namespace Bots
{
    std::string ScriptedMove::Start(std::string const& driver, std::string const& point)
    {
        m_inProgress = true;
        m_waits = false;
        m_driver = driver;
        m_point = point;
        m_stopReason.clear();
        return driver + " moves to " + point;
    }

    std::string ScriptedMove::Arrive()
    {
        if (!m_inProgress)
            return "";
        m_inProgress = false;
        m_waits = false;
        return m_driver + " arrives at " + m_point;
    }

    std::string ScriptedMove::Stop(std::string const& reason, bool inFight)
    {
        if (!m_inProgress || m_waits)
            return "";
        m_stopReason = reason;
        ++m_stopped;
        if (inFight)
        {
            m_waits = true;
            m_quietSince = -1;
            return m_driver + " stops before " + m_point + " for the fight: " + reason;
        }
        m_inProgress = false;
        return m_driver + " stops before " + m_point + ": " + reason;
    }

    bool ScriptedMove::FightEnded(bool groupInCombat, std::int64_t nowMs)
    {
        if (!m_waits)
            return false;
        if (groupInCombat)
        {
            m_quietSince = -1;
            return false;
        }
        if (m_quietSince < 0)
            m_quietSince = nowMs;
        return nowMs - m_quietSince >= CombatEndQuietMs;
    }

    std::string ScriptedMove::GoOn()
    {
        if (!m_waits)
            return "";
        m_waits = false;
        return m_driver + " goes on to " + m_point + " after the fight";
    }

    void ScriptedMove::End()
    {
        m_inProgress = false;
        m_waits = false;
        m_stopReason.clear();
    }
}
