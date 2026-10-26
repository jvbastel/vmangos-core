#ifndef WOWAI_BOTS_SPELL_BOOK_H
#define WOWAI_BOTS_SPELL_BOOK_H

#include "BotTypes.h"
#include "BotView.h"

// The shared spell book helper. Class modules name each spell by the spell ID
// of its first rank, and the helper finds the rank that the bot casts.
namespace Bots
{
    // The highest rank of the spell whose first rank is `firstRank` that the
    // bot knows, or NoSpell when it knows no rank. A spell without ranks is
    // its own first and highest rank.
    SpellId HighestKnownRank(BotView const& view, SpellId firstRank);

    // The highest known rank of the spell whose first rank is `firstRank`
    // that the level of unit `target` allows (BotView::LevelAllowsRank), or
    // NoSpell. A buff on a low-level player takes a lower rank.
    SpellId RankForTarget(BotView const& view, SpellId firstRank, Guid target);

    // The rank of RankForTarget when the bot can cast it on unit `target`
    // now (BotView::CanCast), else NoSpell.
    SpellId CastableRank(BotView const& view, SpellId firstRank, Guid target);
}

#endif
