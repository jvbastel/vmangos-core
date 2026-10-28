#include "PeelWatch.h"

#include "CommonRules.h"
#include "StepBack.h"

#include <cstdio>
#include <set>

namespace Bots
{
    namespace
    {
        std::string OneDecimal(double value)
        {
            char text[32];
            std::snprintf(text, sizeof(text), "%.1f", value);
            return text;
        }
    }

    void PeelWatch::Update(PeelSeen const& seen, std::int64_t nowMs)
    {
        std::set<Guid> present;
        for (PeelEnemySeen const& e : seen.enemies)
        {
            present.insert(e.enemy);
            auto const inserted = m_tracks.emplace(e.enemy, Track());
            Track& t = inserted.first->second;
            if (inserted.second)
                t.health = e.health;

            // The melee hit before this update: the damage of the nova can
            // turn the enemy on the caster in the update that shows the
            // freeze.
            Guid const hitBefore = t.reachMs >= 0 ? t.victim : NoGuid;
            std::int64_t const reachBefore = t.reachMs;
            bool const contact = e.victim != NoGuid && e.victim != seen.tank && e.victimYards <= MeleeHitYards;
            if (!contact)
                t.reachMs = -1;
            else if (t.reachMs < 0 || t.victim != e.victim)
                t.reachMs = nowMs;
            t.victim = e.victim;
            bool const hit = e.health < t.health;
            t.health = e.health;

            if (!t.open && e.frozenBy == NoGuid)
                continue;
            bool const opens = !t.open;
            if (opens)
            {
                // A new freeze: a freeze of the same caster in this update
                // is the same nova.
                std::size_t nova = m_novas.size();
                for (std::size_t i = 0; i < m_novas.size(); ++i)
                    if (m_novas[i].caster == e.frozenBy && m_novas[i].ms == nowMs)
                        nova = i;
                if (nova == m_novas.size())
                {
                    PeelNova fresh;
                    fresh.caster = e.frozenBy;
                    fresh.ms = nowMs;
                    m_novas.push_back(fresh);
                }
                PeelNova& n = m_novas[nova];
                Guid const victim = hitBefore != NoGuid ? hitBefore : e.victim;
                std::int64_t const reachMs = hitBefore != NoGuid ? reachBefore : t.reachMs;
                if (victim != NoGuid && victim != seen.tank && !Contains(n.victims, victim))
                {
                    n.victims.push_back(victim);
                    n.forOthers = n.forOthers || victim != n.caster;
                }
                if (victim == seen.healer && reachMs >= 0 && n.healerReachMs < 0)
                    n.healerReachMs = nowMs - reachMs;

                PeelFreeze freeze;
                freeze.enemy = e.enemy;
                freeze.nova = nova;
                freeze.victim = victim;
                freeze.startMs = nowMs;
                freeze.onHealer = victim != NoGuid && victim == seen.healer;
                t.open = true;
                t.freeze = m_freezes.size();
                t.hitBeforeTank = false;
                m_freezes.push_back(freeze);
            }

            PeelFreeze& f = m_freezes[t.freeze];
            if (e.tankHasIt && f.tankMs < 0)
                f.tankMs = nowMs;
            // The update that opens the freeze shows the damage of the nova
            // itself: only a later hit is party damage.
            if (hit && !opens && f.tankMs < 0)
                t.hitBeforeTank = true;
            if (f.onHealer)
            {
                f.healerYards = e.healerYards;
                f.healerTankYards = seen.healerTankYards;
                f.healerSeesTank = seen.healerSeesTank;
            }
            f.endMs = nowMs;
            if (e.frozenBy == NoGuid)
                EndFreeze(t, true, nowMs);
        }

        for (auto it = m_tracks.begin(); it != m_tracks.end();)
        {
            if (present.count(it->first))
            {
                ++it;
                continue;
            }
            if (it->second.open)
                EndFreeze(it->second, false, nowMs);
            it = m_tracks.erase(it);
        }
    }

    void PeelWatch::EndFreeze(Track& track, bool alive, std::int64_t nowMs)
    {
        PeelFreeze& f = m_freezes[track.freeze];
        f.open = false;
        f.endMs = nowMs;
        f.brokenByParty = alive && track.hitBeforeTank && f.tankMs < 0 && nowMs - f.startMs < FreezeRanOutMs;
        track.open = false;
        track.hitBeforeTank = false;
    }

    std::size_t PeelWatch::NovasForOthers() const
    {
        std::size_t count = 0;
        for (PeelNova const& nova : m_novas)
            if (nova.forOthers)
                ++count;
        return count;
    }

    bool PeelWatch::NovaBrokenByParty(std::size_t nova) const
    {
        for (PeelFreeze const& freeze : m_freezes)
            if (freeze.nova == nova && freeze.brokenByParty)
                return true;
        return false;
    }

    std::size_t PeelWatch::NovasBrokenByParty() const
    {
        std::size_t count = 0;
        for (std::size_t i = 0; i < m_novas.size(); ++i)
            if (NovaBrokenByParty(i))
                ++count;
        return count;
    }

    std::string PeelFailure(PeelWatch const& watch)
    {
        PeelNova const* first = nullptr;
        for (PeelNova const& nova : watch.Novas())
            if (nova.healerReachMs >= 0)
            {
                first = &nova;
                break;
            }
        if (!first)
            return "no Frost Nova froze an enemy on the healer";
        if (first->healerReachMs > PeelNovaLimitMs)
            return "the nova came " + OneDecimal(first->healerReachMs / 1000.0) +
                " s after the enemy reached the healer (limit " + OneDecimal(PeelNovaLimitMs / 1000.0) + " s)";
        if (watch.NovasBrokenByParty() > 0)
            return "party damage broke a freeze before the tank arrived";
        for (PeelFreeze const& freeze : watch.Freezes())
        {
            if (!freeze.onHealer)
                continue;
            if (freeze.healerYards < StepBackYards)
                return "the healer ended " + OneDecimal(freeze.healerYards) + " yd from the frozen enemy (at least " +
                    std::to_string(int(StepBackYards)) + " yd)";
            if (freeze.healerTankYards > StepBackHealYards)
                return "the healer ended " + OneDecimal(freeze.healerTankYards) + " yd from the tank (at most " +
                    std::to_string(int(StepBackHealYards)) + " yd)";
            if (!freeze.healerSeesTank)
                return "the healer ended out of sight of the tank";
        }
        return "";
    }
}
