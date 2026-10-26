#include "WispTrail.h"

namespace Bots
{
    WispTrail::Choice WispTrail::Next(IsOnMap const& isOnMap, std::size_t freeSummons)
    {
        for (auto it = m_wisps.begin(); it != m_wisps.end();)
            it = isOnMap(*it) ? it + 1 : m_wisps.erase(it);
        Choice choice;
        if (m_wisps.size() < WispTrailSize && freeSummons > 0)
            choice.action = Action::Summon;
        else if (!m_wisps.empty())
        {
            choice.action = Action::Move;
            choice.guid = m_wisps.front();
            m_wisps.pop_front();
            m_wisps.push_back(choice.guid);
        }
        return choice;
    }

    void WispTrail::Summoned(Guid guid)
    {
        m_wisps.push_back(guid);
    }

    std::vector<Guid> WispTrail::Clear()
    {
        std::vector<Guid> wisps(m_wisps.begin(), m_wisps.end());
        m_wisps.clear();
        return wisps;
    }

    std::vector<Guid> WispTrail::Wisps() const
    {
        return std::vector<Guid>(m_wisps.begin(), m_wisps.end());
    }
}
