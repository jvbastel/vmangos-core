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

    void ThreatWatch::Update(std::vector<EnemyOnMember> const& hits, std::int64_t nowMs, HitNamer const& name)
    {
        std::vector<Hit> next;
        for (EnemyOnMember const& pair : hits)
        {
            auto found = std::find_if(m_hits.begin(), m_hits.end(), [&](Hit const& hit) { return Same(hit.pair, pair); });
            Hit hit = found != m_hits.end() ? *found : Hit{pair, nowMs, -1};
            if (hit.loss < 0 && nowMs - hit.startMs > ThreatLossMs)
            {
                hit.loss = static_cast<int>(m_losses.size());
                m_losses.push_back({pair.enemy, pair.member, hit.startMs, nowMs, name ? name(pair) : std::string()});
            }
            if (hit.loss >= 0)
                m_losses[hit.loss].endMs = nowMs;
            if (std::none_of(next.begin(), next.end(), [&](Hit const& kept) { return Same(kept.pair, pair); }))
                next.push_back(hit);
        }
        m_hits = std::move(next);
    }

    std::vector<Guid> AttackedEnemies(std::vector<MemberAttacks> const& members)
    {
        std::vector<Guid> attacked;
        for (MemberAttacks const& member : members)
        {
            if (!member.bot)
                continue;
            for (Guid enemy : {member.victim, member.petVictim})
                if (enemy != NoGuid)
                    attacked.push_back(enemy);
            for (Guid enemy : member.spellTargets)
                if (enemy != NoGuid)
                    attacked.push_back(enemy);
        }
        return attacked;
    }

    void LostMobWatch::Update(std::vector<EnemyOnMember> const& hits, std::vector<Guid> const& attacked, std::int64_t nowMs,
        Guid holder, HitNamer const& name)
    {
        std::vector<Unattacked> next;
        for (EnemyOnMember const& hit : hits)
        {
            if ((holder != NoGuid && hit.member == holder)
                || Contains(attacked, hit.enemy)
                || std::any_of(next.begin(), next.end(), [&](Unattacked const& kept) { return kept.enemy == hit.enemy; }))
                continue;
            auto found = std::find_if(m_unattacked.begin(), m_unattacked.end(),
                [&](Unattacked const& old) { return old.enemy == hit.enemy; });
            Unattacked const now = found != m_unattacked.end() ? *found : Unattacked{hit.enemy, nowMs};
            next.push_back(now);
            auto lost = std::find_if(m_lost.begin(), m_lost.end(), [&](LostMob const& mob) { return mob.enemy == hit.enemy; });
            if (lost != m_lost.end())
            {
                if (lost->startMs == now.startMs)
                {
                    lost->member = hit.member;
                    lost->endMs = nowMs;
                }
            }
            else if (nowMs - now.startMs > m_limitMs)
                m_lost.push_back({hit.enemy, hit.member, now.startMs, nowMs, name ? name(hit) : std::string()});
        }
        m_unattacked = std::move(next);
    }
}

// jvbastel/WOWAI#174: the first real hit on the healer.
namespace Bots
{
    std::vector<EnemyOnMember> RealHitGate::Gate(
        std::vector<EnemyOnMember> const& hits, std::set<Guid> const& healers) const
    {
        std::vector<EnemyOnMember> gated;
        for (EnemyOnMember const& hit : hits)
            if (!healers.count(hit.member) || HasHit(hit.enemy, hit.member))
                gated.push_back(hit);
        return gated;
    }
}

// jvbastel/WOWAI#174: the lost-mob clock from the first real hit, with a
// backstop.
namespace Bots
{
    void RealHitLostMobWatch::Update(std::vector<EnemyOnMember> const& hits, RealHitGate const& gate,
        std::vector<Guid> const& attacked, std::int64_t nowMs, Guid holder, HitNamer const& name)
    {
        std::set<Guid> members;
        for (EnemyOnMember const& hit : hits)
            members.insert(hit.member);
        std::size_t const hitBefore = m_hitWatch.LostMobs().size(), backstopBefore = m_backstop.LostMobs().size();
        m_hitWatch.Update(gate.Gate(hits, members), attacked, nowMs, holder, name);
        m_backstop.Update(hits, attacked, nowMs, holder, name);
        auto const add = [this](LostMob const& mob) {
            for (LostMob const& lost : m_lost)
                if (lost.enemy == mob.enemy)
                    return;
            m_lost.push_back(mob);
        };
        for (std::size_t i = hitBefore; i < m_hitWatch.LostMobs().size(); ++i)
            add(m_hitWatch.LostMobs()[i]);
        for (std::size_t i = backstopBefore; i < m_backstop.LostMobs().size(); ++i)
            add(m_backstop.LostMobs()[i]);
    }
}
