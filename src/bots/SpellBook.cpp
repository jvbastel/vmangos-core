#include "SpellBook.h"

namespace Bots
{
    namespace
    {
        // The highest rank in the chain from `firstRank` that the bot knows
        // and that `allows` accepts. A bot can miss a rank in the middle, so
        // the walk goes through the whole chain.
        template <typename Allows>
        SpellId HighestRank(BotView const& view, SpellId firstRank, Allows allows)
        {
            SpellId highest = NoSpell;
            for (SpellId rank = firstRank; rank != NoSpell; rank = view.GetNextRank(rank))
                if (view.KnowsSpell(rank) && allows(rank))
                    highest = rank;
            return highest;
        }
    }

    SpellId HighestKnownRank(BotView const& view, SpellId firstRank)
    {
        return HighestRank(view, firstRank, [](SpellId) { return true; });
    }

    SpellId RankForTarget(BotView const& view, SpellId firstRank, Guid target)
    {
        return HighestRank(view, firstRank, [&view, target](SpellId rank) { return view.LevelAllowsRank(rank, target); });
    }

    SpellId CastableRank(BotView const& view, SpellId firstRank, Guid target)
    {
        SpellId const rank = RankForTarget(view, firstRank, target);
        return rank != NoSpell && !view.IsCastLocked(rank) && view.CanCast(rank, target) ? rank : NoSpell;
    }

    SpellId CastableHealRank(BotView const& view, SpellId firstRank, Guid target)
    {
        Health const health = view.GetHealth(target);
        std::uint32_t const missing = health.max > health.current ? health.max - health.current : 0;
        std::uint32_t const mana = view.GetMana(view.GetSelf()).current;
        SpellId highest = NoSpell;
        for (SpellId rank = firstRank; rank != NoSpell; rank = view.GetNextRank(rank))
        {
            HealRank const heal = view.GetHealRank(rank);
            if (!view.KnowsSpell(rank) || !view.LevelAllowsRank(rank, target) || heal.manaCost > mana
                || view.IsCastLocked(rank) || !view.CanCast(rank, target))
                continue;
            if (heal.averageHeal >= missing)
                return rank;
            highest = rank;
        }
        return highest;
    }
}
