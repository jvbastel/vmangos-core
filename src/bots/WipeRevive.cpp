#include "WipeRevive.h"

#include <algorithm>

namespace Bots
{
    bool WipeWatch::Update(std::vector<RunMember> const& members, std::int64_t nowMs)
    {
        bool const someoneDead = std::find(members.begin(), members.end(), RunMember::Dead) != members.end();
        bool const wiped = someoneDead && std::all_of(members.begin(), members.end(), [](RunMember member) {
            return member == RunMember::Dead || member == RunMember::NotInWorld;
        });
        if (!wiped)
        {
            m_wipeMs = -1;
            return false;
        }
        if (m_wipeMs < 0)
            m_wipeMs = nowMs;
        if (nowMs - m_wipeMs < WipeReviveDelayMs)
            return false;
        m_wipeMs = -1;
        return true;
    }
}
