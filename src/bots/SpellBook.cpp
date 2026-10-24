#include "SpellBook.h"

namespace Bots
{
    SpellId HighestKnownRank(BotView const& view, SpellId firstRank)
    {
        // A bot can miss a rank in the middle, so the walk goes through the
        // whole chain.
        SpellId highest = NoSpell;
        for (SpellId rank = firstRank; rank != NoSpell; rank = view.GetNextRank(rank))
            if (view.KnowsSpell(rank))
                highest = rank;
        return highest;
    }
}
