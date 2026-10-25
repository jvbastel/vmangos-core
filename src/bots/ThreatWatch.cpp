#include "ThreatWatch.h"

#include <algorithm>

namespace Bots
{
    namespace
    {
        bool Same(EnemyOnMember const& a, EnemyOnMember const& b)
        {
            return a.enemy == b.enemy && a.member == b.member;
        }
    }

    void ThreatWatch::Update(std::vector<EnemyOnMember> const& hits, std::int64_t nowMs)
    {
        std::vector<Hit> next;
        for (EnemyOnMember const& pair : hits)
        {
            auto found = std::find_if(m_hits.begin(), m_hits.end(), [&](Hit const& hit) { return Same(hit.pair, pair); });
            Hit hit = found != m_hits.end() ? *found : Hit{pair, nowMs, -1};
            if (hit.loss < 0 && nowMs - hit.startMs > ThreatLossMs)
            {
                hit.loss = static_cast<int>(m_losses.size());
                m_losses.push_back({pair.enemy, pair.member, hit.startMs, nowMs});
            }
            if (hit.loss >= 0)
                m_losses[hit.loss].endMs = nowMs;
            if (std::none_of(next.begin(), next.end(), [&](Hit const& kept) { return Same(kept.pair, pair); }))
                next.push_back(hit);
        }
        m_hits = std::move(next);
    }
}
