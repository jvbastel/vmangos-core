#include "Trail.h"
#include "BotGeometry.h"
#include "CommonRules.h"
#include "GroupCoordinator.h"

namespace Bots
{
    void Trail::Note(Position const& at)
    {
        if (!m_points.empty())
        {
            float const step = Distance3d(m_points.back(), at);
            if (step > TrailJumpYards)
                m_points.clear();
            else if (step < TrailStepYards)
                return;
        }
        m_points.push_back(at);
        if (m_points.size() > TrailMostPoints)
            m_points.erase(m_points.begin(), m_points.begin() + (m_points.size() - TrailMostPoints));
    }

    void Trail::CutBackTo(Position const& at)
    {
        if (m_points.size() < 2)
            return;
        std::size_t nearest = m_points.size();
        float nearestYards = TrailStepYards;
        for (std::size_t i = 0; i + 1 < m_points.size(); ++i)
        {
            float const yards = Distance3d(m_points[i], at);
            if (yards <= nearestYards)
            {
                nearest = i;
                nearestYards = yards;
            }
        }
        if (nearest < m_points.size())
            m_points.erase(m_points.begin() + std::ptrdiff_t(nearest) + 1, m_points.end());
    }

    void Trail::Clear()
    {
        m_points.clear();
    }

    void WatchTrail(BotView const& view, GroupCoordinator& group)
    {
        Guid const self = view.GetSelf();
        group.KeepTrail(view.GetPosition(self), view.IsInCombat(self) || !IsAlive(view, self));
    }
}
