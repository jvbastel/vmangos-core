#include "SpellBook.h"

#include <vector>

namespace Bots
{
    namespace
    {
        // The ranks in the chain from `firstRank` that the bot knows and
        // that `allows` accepts, lowest first. A bot can miss a rank in the
        // middle, so the walk goes through the whole chain.
        template <typename Allows>
        std::vector<SpellId> KnownRanks(BotView const& view, SpellId firstRank, Allows allows)
        {
            std::vector<SpellId> ranks;
            for (SpellId rank = firstRank; rank != NoSpell; rank = view.GetNextRank(rank))
                if (view.KnowsSpell(rank) && allows(rank))
                    ranks.push_back(rank);
            return ranks;
        }

        // The highest of KnownRanks, or NoSpell.
        template <typename Allows>
        SpellId HighestRank(BotView const& view, SpellId firstRank, Allows allows)
        {
            std::vector<SpellId> const ranks = KnownRanks(view, firstRank, allows);
            return ranks.empty() ? NoSpell : ranks.back();
        }

        // No stun or silence locks the cast of `rank` (BotView::IsCastLocked),
        // and BotView::CanCast allows it on unit `target` now.
        bool CanCastNow(BotView const& view, SpellId rank, Guid target)
        {
            return !view.IsCastLocked(rank) && view.CanCast(rank, target);
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
        return rank != NoSpell && CanCastNow(view, rank, target) ? rank : NoSpell;
    }

    SpellId CastableHealRank(BotView const& view, SpellId firstRank, Guid target)
    {
        Health const health = view.GetHealth(target);
        std::uint32_t const missing = health.max > health.current ? health.max - health.current : 0;
        std::uint32_t const mana = view.GetMana(view.GetSelf()).current;
        // The real CanCast checks the mana too. The mana check here keeps
        // the choice inside BotView's contract, so the fake bot models it
        // and a low-mana bot drops to a cheaper rank.
        std::vector<SpellId> const ranks = KnownRanks(view, firstRank, [&view, target, mana](SpellId rank) {
            return view.LevelAllowsRank(rank, target) && view.GetRankAmounts(rank).manaCost <= mana
                && CanCastNow(view, rank, target);
        });
        for (SpellId rank : ranks)
            if (view.GetRankAmounts(rank).averageHeal >= missing)
                return rank;
        return ranks.empty() ? NoSpell : ranks.back();
    }
}
