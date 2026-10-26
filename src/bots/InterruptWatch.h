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
//   and each interrupt that lands (the cast stops):
//     watch.NoteAttempt(member, enemy, castSpell, nowMs);
//     watch.NoteLanded(member, enemy, castSpell, kind, nowMs);
//
// A missed cast is an interruptible cast that ended with no landed
// interrupt, while the caster lives on, and while a member was free at an
// update of the cast. A double interrupt is an attempt of a second member at
// a cast that another member interrupts: the second attempt comes at or
// after the attempt that lands, so it was a waste. A second try after an
// attempt that missed (a dodge) is no double.
namespace Bots
{
    // A landed interrupt can reach the watch after the update that sees its
    // cast end: the watch waits this long before a cast counts as missed.
    std::int64_t const InterruptNoteGraceMs = 2000;
    // An attempt of another member at most this long after the landed
    // interrupt is a double: the school lockout of Kick and Shield Bash is
    // longer, so the caster cannot cast that spell again in between.
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
        Guid first;      // The member whose interrupt landed.
        Guid second;     // The member whose attempt was a waste.
        std::int64_t ms; // The second attempt.
    };

    class InterruptWatch
    {
    public:
        // An enemy missing from `casts` went away: its cast is no miss.
        void Observe(std::vector<EnemyCastSeen> const& casts, bool interrupterFree, std::int64_t nowMs);
        void NoteAttempt(Guid member, Guid enemy, SpellId castSpell, std::int64_t nowMs);
        void NoteLanded(Guid member, Guid enemy, SpellId castSpell, CastKind kind, std::int64_t nowMs);

        std::vector<InterruptDone> const& Done() const { return m_done; }
        std::vector<MissedCast> const& Missed() const { return m_missed; }
        std::vector<DoubleInterrupt> const& Doubles() const { return m_doubles; }

    private:
        struct Cast
        {
            Guid enemy;
            SpellId spell;
            CastKind kind;
            std::int64_t startMs;
            std::int64_t endMs; // 0 while it lasts.
            bool free;
            bool interrupted;
        };

        struct Attempt
        {
            Guid member;
            Guid enemy;
            SpellId spell;
            std::int64_t ms;
            bool doubled; // It counts in a double already.
        };

        // Attempt `attempt` wasted the cast that `done` interrupted.
        bool IsDouble(Attempt const& attempt, InterruptDone const& done) const;
        void AddDouble(Attempt& attempt, InterruptDone const& done);

        std::vector<Cast> m_casts; // The casts that last, and those that wait for a late note.
        std::vector<Attempt> m_attempts;
        std::vector<InterruptDone> m_done;
        std::vector<MissedCast> m_missed;
        std::vector<DoubleInterrupt> m_doubles;
    };
}

#endif
