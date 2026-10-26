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
                cast.interrupted = true;
        }

        // The ended casts whose grace is over.
        for (Cast const& cast : m_casts)
            if (cast.endMs && nowMs - cast.endMs > InterruptNoteGraceMs && !cast.interrupted && cast.free)
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
        m_attempts.push_back({member, enemy, castSpell, nowMs, false});
        for (InterruptDone const& done : m_done)
            if (IsDouble(m_attempts.back(), done))
            {
                AddDouble(m_attempts.back(), done);
                break;
            }
    }

    void InterruptWatch::NoteLanded(Guid member, Guid enemy, SpellId castSpell, CastKind kind, std::int64_t nowMs)
    {
        m_done.push_back({member, enemy, castSpell, kind, nowMs});
        for (Cast& cast : m_casts)
            if (cast.enemy == enemy && cast.spell == castSpell)
                cast.interrupted = true;
        for (Attempt& attempt : m_attempts)
            if (IsDouble(attempt, m_done.back()))
                AddDouble(attempt, m_done.back());
    }

    bool InterruptWatch::IsDouble(Attempt const& attempt, InterruptDone const& done) const
    {
        if (attempt.doubled || attempt.member == done.member || attempt.enemy != done.enemy || attempt.spell != done.spell)
            return false;
        if (attempt.ms > done.ms + DoubleInterruptMs)
            return false;
        // The attempt of the member whose interrupt landed: its last one
        // at the cast before it landed.
        std::int64_t landedAttemptMs = done.ms;
        for (Attempt const& own : m_attempts)
            if (own.member == done.member && own.enemy == done.enemy && own.spell == done.spell && own.ms <= done.ms
                && done.ms - own.ms <= DoubleInterruptMs)
                landedAttemptMs = own.ms;
        return attempt.ms >= landedAttemptMs;
    }

    void InterruptWatch::AddDouble(Attempt& attempt, InterruptDone const& done)
    {
        attempt.doubled = true;
        m_doubles.push_back({done.enemy, done.spell, done.member, attempt.member, attempt.ms});
    }
}
