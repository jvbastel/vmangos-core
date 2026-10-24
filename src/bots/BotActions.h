#ifndef WOWAI_BOTS_BOT_ACTIONS_H
#define WOWAI_BOTS_BOT_ACTIONS_H

#include "BotTypes.h"

namespace Bots
{
    // The deeds of one bot in the game. With BotView, the only way from the
    // decision library to the game (ADR 0002 of WOWAI).
    class BotActions
    {
    public:
        virtual ~BotActions() = default;

        // Cast spell `spell` on unit `target`.
        virtual void CastSpell(SpellId spell, Guid target) = 0;
        // Move to `destination`.
        virtual void MoveTo(Position const& destination) = 0;
        // Start the melee attack of unit `target`.
        virtual void Attack(Guid target) = 0;
        // Run to unit `target` and stay in melee range of it.
        virtual void Chase(Guid target) = 0;
        // Run behind unit `target` and stay in melee range there.
        virtual void ChaseBehind(Guid target) = 0;
        // Run after unit `target` until it is about `yards` away, for a
        // ranged bot: it moves again when the unit gets far away.
        virtual void ChaseInRange(Guid target, float yards) = 0;
        // The pet of the bot attacks unit `target`.
        virtual void PetAttack(Guid target) = 0;
        // Follow unit `unit` at a short distance.
        virtual void Follow(Guid unit) = 0;
    };
}

#endif
