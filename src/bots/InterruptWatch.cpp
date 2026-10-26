#include "InterruptWatch.h"

#include <algorithm>

namespace Bots
{
    void InterruptWatch::Observe(std::vector<EnemyCastSeen> const& casts, bool interrupterFree, std::int64_t nowMs)
    {
        // The casts that ended.
        for (Cast& cast : m_casts)
        {
            if (cast.endMs)
                continue;
            auto const seen = std::find_if(casts.begin(), casts.end(),
                [&cast](EnemyCastSeen const& enemy) { return enemy.enemy == cast.enemy; });
            if (seen != casts.end() && seen->alive && seen->spell == cast.spell)
            {
                cast.free = cast.free || interrupterFree;
                continue;
            }
            cast.endMs = nowMs;
            // A caster that died or went away: no miss.
            if (seen == casts.end() || !seen->alive)
                cast.stopped = true;
        }

        // The ended casts whose grace is over.
        for (Cast const& cast : m_casts)
            if (cast.endMs && nowMs - cast.endMs > InterruptNoteGraceMs && !cast.stopped && cast.free)
                m_missed.push_back({cast.enemy, cast.spell, cast.kind, cast.startMs});
        m_casts.erase(std::remove_if(m_casts.begin(), m_casts.end(),
            [nowMs](Cast const& cast) { return cast.endMs && nowMs - cast.endMs > InterruptNoteGraceMs; }),
            m_casts.end());

        // The new casts.
        for (EnemyCastSeen const& enemy : casts)
        {
            if (!enemy.alive || enemy.spell == NoSpell)
                continue;
            bool const known = std::any_of(m_casts.begin(), m_casts.end(), [&enemy](Cast const& cast) {
                return !cast.endMs && cast.enemy == enemy.enemy && cast.spell == enemy.spell;
            });
            if (!known)
                m_casts.push_back({enemy.enemy, enemy.spell, enemy.kind, nowMs, 0, interrupterFree, false});
        }
    }

    void InterruptWatch::NoteAttempt(Guid member, Guid enemy, SpellId castSpell, std::int64_t nowMs)
    {
        m_attempts.push_back({member, enemy, castSpell, nowMs, false, false});
    }

    void InterruptWatch::NoteMissed(Guid member, Guid enemy, std::int64_t nowMs)
    {
        for (auto it = m_attempts.rbegin(); it != m_attempts.rend(); ++it)
            if (it->member == member && it->enemy == enemy && nowMs - it->ms <= InterruptLandsMs)
            {
                it->missed = true;
                return;
            }
    }

    void InterruptWatch::NoteStopped(Guid enemy, SpellId castSpell, CastKind kind, std::int64_t nowMs)
    {
        for (Cast& cast : m_casts)
            if (cast.enemy == enemy && cast.spell == castSpell)
                cast.stopped = true;
        // The first attempt at the cast that did not miss landed, once.
        for (Attempt const& attempt : m_attempts)
            if (attempt.landed && attempt.enemy == enemy && attempt.spell == castSpell
                && nowMs - attempt.ms <= InterruptLandsMs)
                return;
        for (Attempt& attempt : m_attempts)
            if (!attempt.missed && attempt.enemy == enemy && attempt.spell == castSpell && attempt.ms <= nowMs
                && nowMs - attempt.ms <= InterruptLandsMs)
            {
                attempt.landed = true;
                m_done.push_back({attempt.member, enemy, castSpell, kind, nowMs});
                return;
            }
    }

    std::vector<DoubleInterrupt> InterruptWatch::Doubles() const
    {
        std::vector<DoubleInterrupt> doubles;
        for (std::size_t j = 0; j < m_attempts.size(); ++j)
        {
            Attempt const& second = m_attempts[j];
            for (std::size_t i = 0; i < j; ++i)
            {
                Attempt const& first = m_attempts[i];
                if (!first.missed && first.member != second.member && first.enemy == second.enemy
                    && first.spell == second.spell && second.ms - first.ms <= DoubleInterruptMs)
                {
                    doubles.push_back({second.enemy, second.spell, first.member, second.member, second.ms});
                    break;
                }
            }
        }
        return doubles;
    }
}
