#ifndef WOWAI_BOTS_INTERRUPT_WATCH_H
#define WOWAI_BOTS_INTERRUPT_WATCH_H

#include "BotTypes.h"
#include "BotView.h"

#include <cstdint>
#include <vector>

// The interrupts of a scripted run (spec section 10, Results and pass
// thresholds): the interrupts done, the casts missed, and the double
// interrupts. The game layer tells the watch:
// - at each update of the run, the casts that the enemies in the fight can
//   be interrupted in now, and whether a group member could interrupt now
//   (it knows an interrupt spell that is ready, and has the power for it):
//     watch.Observe(casts, interrupterFree, nowMs);
// - each interrupt spell that a member casts at an enemy cast (an attempt),
//   each attempt that missed (a dodge, a parry), and each enemy cast that
//   stopped before it went off:
//     watch.NoteAttempt(member, enemy, castSpell, nowMs);
//     watch.NoteMissed(member, enemy, nowMs);
//     watch.NoteStopped(enemy, castSpell, kind, nowMs);
//
// An interrupt is done when a cast stops at most InterruptLandsMs after an
// attempt at it that did not miss: the first such attempt landed. A cast
// that stops with no attempt (a stun, the caster died) is neither done nor
// missed. A missed cast is an interruptible cast that ended without a stop,
// while the caster lives on, and while a member was free at an update of the
// cast. A double interrupt is a second attempt of another member at the same
// cast, at most DoubleInterruptMs after an attempt that did not miss: two
// bots spent their interrupt on one cast. A second try after an attempt that
// missed is no double.
namespace Bots
{
    // The note of a stop can reach the watch after the update that sees its
    // cast end: the watch waits this long before a cast counts as missed.
    std::int64_t const InterruptNoteGraceMs = 2000;
    // The spell batches of the core delay the effect of an interrupt: a stop
    // this long after an attempt is its interrupt.
    std::int64_t const InterruptLandsMs = 1000;
    // Two attempts at one cast this near each other are a double. The
    // school lockout of Kick and Shield Bash is longer, so the caster cannot
    // cast that spell again in between.
    std::int64_t const DoubleInterruptMs = 1000;

    // An enemy in the fight at one update.
    struct EnemyCastSeen
    {
        Guid enemy;
        bool alive;
        SpellId spell; // NoSpell: no cast that an interrupt can stop now.
        CastKind kind;
    };

    struct InterruptDone
    {
        Guid member;
        Guid enemy;
        SpellId spell; // The interrupted cast.
        CastKind kind;
        std::int64_t ms;
    };

    struct MissedCast
    {
        Guid enemy;
        SpellId spell;
        CastKind kind;
        std::int64_t ms; // The first update that saw the cast.
    };

    struct DoubleInterrupt
    {
        Guid enemy;
        SpellId spell;
        Guid first;      // The member whose attempt came first.
        Guid second;     // The member whose attempt was a waste.
        std::int64_t ms; // The second attempt.
    };

    class InterruptWatch
    {
    public:
        // An enemy missing from `casts` went away: its cast is no miss.
        void Observe(std::vector<EnemyCastSeen> const& casts, bool interrupterFree, std::int64_t nowMs);
        void NoteAttempt(Guid member, Guid enemy, SpellId castSpell, std::int64_t nowMs);
        // The last attempt of the member at the enemy missed.
        void NoteMissed(Guid member, Guid enemy, std::int64_t nowMs);
        // Each member that sees the stop can note it: it counts once.
        void NoteStopped(Guid enemy, SpellId castSpell, CastKind kind, std::int64_t nowMs);

        std::vector<InterruptDone> const& Done() const { return m_done; }
        std::vector<MissedCast> const& Missed() const { return m_missed; }
        // From the attempts so far: a miss note can come after the second
        // attempt.
        std::vector<DoubleInterrupt> Doubles() const;

    private:
        struct Cast
        {
            Guid enemy;
            SpellId spell;
            CastKind kind;
            std::int64_t startMs;
            std::int64_t endMs; // 0 while it lasts.
            bool free;
            bool stopped; // Or the caster died or went away: no miss.
        };

        struct Attempt
        {
            Guid member;
            Guid enemy;
            SpellId spell;
            std::int64_t ms;
            bool missed;
            bool landed;
        };

        std::vector<Cast> m_casts; // The casts that last, and those that wait for a late note.
        std::vector<Attempt> m_attempts;
        std::vector<InterruptDone> m_done;
        std::vector<MissedCast> m_missed;
    };
}

#endif
