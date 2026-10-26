#include "ThinkTimer.h"

namespace Bots
{
    ThinkState ThinkStateFor(bool inCombat, bool nearPlayer)
    {
        if (inCombat)
            return ThinkState::Combat;
        return nearPlayer ? ThinkState::NearPlayer : ThinkState::Far;
    }

    std::uint32_t ThinkInterval(ThinkState state, ThinkSettings const& settings)
    {
        switch (state)
        {
            case ThinkState::Combat:
                return settings.combatMs;
            case ThinkState::NearPlayer:
                return settings.nearPlayerMs;
            case ThinkState::Far:
                break;
        }
        return settings.farMs;
    }

    ThinkTimer::ThinkTimer(ThinkSettings settings)
        : m_settings(settings), m_sinceThinkMs(0), m_thoughtOnce(false)
    {
    }

    bool ThinkTimer::Advance(std::uint32_t diffMs, ThinkState state)
    {
        m_sinceThinkMs += diffMs;
        if (m_thoughtOnce && m_sinceThinkMs < ThinkInterval(state, m_settings))
            return false;
        m_thoughtOnce = true;
        m_sinceThinkMs = 0;
        return true;
    }
}
