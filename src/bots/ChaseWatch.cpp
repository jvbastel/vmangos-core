#include "ChaseWatch.h"

#include <algorithm>

namespace Bots
{
    void ChaseWatch::Update(std::vector<MemberChase> const& chases, std::int64_t nowMs)
    {
        std::vector<std::size_t> open;
        for (MemberChase const& chase : chases)
        {
            if (chase.yardsFromTank <= ChaseRecordYards)
                continue;
            auto const last = std::find_if(m_open.begin(), m_open.end(),
                [this, &chase](std::size_t index) { return m_chases[index].member == chase.member; });
            if (last == m_open.end())
            {
                m_chases.push_back({chase.member, chase.enemy, nowMs, nowMs, chase.yardsFromTank});
                open.push_back(m_chases.size() - 1);
                continue;
            }
            FarChase& far = m_chases[*last];
            far.endMs = nowMs;
            far.maxYards = std::max(far.maxYards, chase.yardsFromTank);
            open.push_back(*last);
        }
        m_open.swap(open);
    }
}
