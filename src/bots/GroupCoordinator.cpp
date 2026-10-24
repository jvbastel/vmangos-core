#include "GroupCoordinator.h"

#include <algorithm>

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
        if (m_pull.phase == PullPhase::None)
            return m_focusTarget != NoGuid;
        return m_pull.phase == PullPhase::Holding;
    }

    bool GroupCoordinator::ClaimInterrupt(Guid caster, SpellId spell, Guid bot)
    {
        Lock lock(m_mutex);
        auto found = m_interruptClaims.find(caster);
        if (found != m_interruptClaims.end() && found->second.spell == spell && found->second.bot != bot)
            return false;
        m_interruptClaims[caster] = {caster, spell, bot};
        return true;
    }

    Guid GroupCoordinator::GetInterruptClaim(Guid caster, SpellId spell) const
    {
        Lock lock(m_mutex);
        auto found = m_interruptClaims.find(caster);
        return found != m_interruptClaims.end() && found->second.spell == spell ? found->second.bot : NoGuid;
    }

    std::vector<InterruptClaim> GroupCoordinator::GetInterruptClaims() const
    {
        Lock lock(m_mutex);
        std::vector<InterruptClaim> claims;
        for (auto const& pair : m_interruptClaims)
            claims.push_back(pair.second);
        return claims;
    }

    void GroupCoordinator::ReleaseInterrupt(Guid caster)
    {
        Lock lock(m_mutex);
        m_interruptClaims.erase(caster);
    }

    void GroupCoordinator::MarkEnemyHealer(Guid enemy)
    {
        Lock lock(m_mutex);
        if (std::find(m_enemyHealers.begin(), m_enemyHealers.end(), enemy) == m_enemyHealers.end())
            m_enemyHealers.push_back(enemy);
    }

    void GroupCoordinator::ForgetEnemyHealer(Guid enemy)
    {
        Lock lock(m_mutex);
        m_enemyHealers.erase(std::remove(m_enemyHealers.begin(), m_enemyHealers.end(), enemy), m_enemyHealers.end());
    }

    std::vector<Guid> GroupCoordinator::GetEnemyHealers() const
    {
        Lock lock(m_mutex);
        return m_enemyHealers;
    }

    Guid GroupCoordinator::GetTank() const
    {
        Lock lock(m_mutex);
        return m_tank;
    }

    void GroupCoordinator::SetTank(Guid tank)
    {
        Lock lock(m_mutex);
        m_tank = tank;
    }
}
