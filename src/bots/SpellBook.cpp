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

    SpellId RankForTarget(BotView const& view, SpellId firstRank, Guid target)
    {
        SpellId highest = NoSpell;
        for (SpellId rank = firstRank; rank != NoSpell; rank = view.GetNextRank(rank))
            if (view.KnowsSpell(rank) && view.LevelAllowsRank(rank, target))
                highest = rank;
        return highest;
    }

    SpellId CastableRank(BotView const& view, SpellId firstRank, Guid target)
    {
        SpellId const rank = RankForTarget(view, firstRank, target);
        return rank != NoSpell && view.CanCast(rank, target) ? rank : NoSpell;
    }
}
