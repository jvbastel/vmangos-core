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
        // The newest such point: the walk back cuts as few points as it can.
        for (std::size_t i = m_points.size() - 1; i-- > 0;)
            if (Distance3d(m_points[i], at) <= TrailStepYards)
            {
                m_points.erase(m_points.begin() + std::ptrdiff_t(i) + 1, m_points.end());
                return;
            }
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
