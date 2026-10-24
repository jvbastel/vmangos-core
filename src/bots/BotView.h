#ifndef WOWAI_BOTS_BOT_VIEW_H
#define WOWAI_BOTS_BOT_VIEW_H

#include "BotTypes.h"

namespace Bots
{
    // The queries of one bot about the game. With BotActions, the only way
    // from the decision library to the game (ADR 0002 of WOWAI). The real
    // adapter in src/game/Bots wraps Player and Unit, and the fake adapter in
    // tests/ serves the tests. Each new query that a trigger needs is a new
    // call here, and both adapters implement it.
    class BotView
    {
    public:
        virtual ~BotView() = default;

        // The GUID of the bot itself.
        virtual Guid GetSelf() const = 0;
        // The health of unit `unit`. {0, 0} for a unit that the bot cannot see.
        virtual Health GetHealth(Guid unit) const = 0;
        // The spell that unit `unit` casts now, or NoSpell.
        virtual SpellId GetCurrentCast(Guid unit) const = 0;

        // Spells and ranks, for the spell book helper (SpellBook.h).
        // True when the bot knows spell `spell`.
        virtual bool KnowsSpell(SpellId spell) const = 0;
        // The next rank of spell `spell` in its chain of ranks, or NoSpell
        // for the highest rank and for a spell without ranks.
        virtual SpellId GetNextRank(SpellId spell) const = 0;
    };
}

#endif
