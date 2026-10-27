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
