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
    };
}

#endif
