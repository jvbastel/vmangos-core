#include "ScriptedMove.h"

namespace Bots
{
    std::string ScriptedMove::Start(std::string const& driver, std::string const& point)
    {
        m_inProgress = true;
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
        return m_driver + " arrives at " + m_point;
    }

    std::string ScriptedMove::Stop(std::string const& reason)
    {
        if (!m_inProgress)
            return "";
        m_inProgress = false;
        m_stopReason = reason;
        ++m_stopped;
        return m_driver + " stops before " + m_point + ": " + reason;
    }

    void ScriptedMove::End()
    {
        m_inProgress = false;
        m_stopReason.clear();
    }
}
