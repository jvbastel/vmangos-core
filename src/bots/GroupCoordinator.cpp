#include "GroupCoordinator.h"

namespace Bots
{
    using Lock = std::lock_guard<std::mutex>;

    Guid GroupCoordinator::GetFocusTarget() const
    {
        Lock lock(m_mutex);
        return m_focusTarget;
    }

    void GroupCoordinator::SetFocusTarget(Guid target)
    {
        Lock lock(m_mutex);
        m_focusTarget = target;
    }

    PullState GroupCoordinator::GetPull() const
    {
        Lock lock(m_mutex);
        return m_pull;
    }

    void GroupCoordinator::StartPull(Guid puller, Guid target)
    {
        Lock lock(m_mutex);
        m_pull = {PullPhase::Pulling, puller, target};
        m_focusTarget = target;
    }

    void GroupCoordinator::HoldPull(Guid puller)
    {
        Lock lock(m_mutex);
        if (m_pull.phase == PullPhase::Pulling && m_pull.puller == puller)
            m_pull.phase = PullPhase::Holding;
    }

    void GroupCoordinator::EndPull()
    {
        Lock lock(m_mutex);
        m_pull = {PullPhase::None, NoGuid, NoGuid};
        m_focusTarget = NoGuid;
    }

    bool GroupCoordinator::TankHoldsThreat() const
    {
        Lock lock(m_mutex);
        return m_pull.phase == PullPhase::Holding;
    }
}
