#include "Loot.h"

namespace Bots
{
    double const ClearUpgradeFactor = 1.1;

    LootRoll ChooseLootRoll(bool canUse, bool clearUpgrade)
    {
        return canUse && clearUpgrade ? LootRoll::Need : LootRoll::Greed;
    }

    GearScores::GearScores(GearTemplate const& gearTemplate)
    {
        for (GearItem const& item : gearTemplate.items)
            m_scores[item.entry] = item.score;
    }

    bool GearScores::IsClearUpgrade(std::uint32_t entry, std::vector<GearOption> const& options) const
    {
        return BestUpgradeOption(entry, options) < options.size();
    }

    std::size_t GearScores::BestUpgradeOption(std::uint32_t entry, std::vector<GearOption> const& options) const
    {
        std::size_t best = options.size();
        auto const found = m_scores.find(entry);
        if (found == m_scores.end())
            return best;
        double const score = found->second;

        double bestGain = 0.0;
        for (std::size_t index = 0; index < options.size(); ++index)
        {
            double worn = 0.0;
            bool known = true;
            for (WornItem const& item : options[index])
            {
                auto const wornScore = m_scores.find(item.entry);
                if (wornScore != m_scores.end())
                    worn += wornScore->second;
                else if (item.grade == ItemGrade::High)
                    known = false;
            }
            // A small tolerance, so that exactly a tenth more counts.
            bool const clear = known && score > worn && score >= worn * ClearUpgradeFactor - 1e-9;
            if (clear && (best == options.size() || score - worn > bestGain))
            {
                best = index;
                bestGain = score - worn;
            }
        }
        return best;
    }

    BagRoom ChooseBagRoom(std::vector<BagItem> const& bagItems, IncomingItem const& incoming)
    {
        BagRoom cheapest = {false, 0};
        for (std::size_t index = 0; index < bagItems.size(); ++index)
        {
            BagItem const& item = bagItems[index];
            if (item.grey && (!cheapest.deletes || item.value < bagItems[cheapest.index].value))
                cheapest = {true, index};
        }
        if (cheapest.deletes && (incoming.usable || incoming.value > bagItems[cheapest.index].value))
            return cheapest;
        return {false, 0};
    }
}
