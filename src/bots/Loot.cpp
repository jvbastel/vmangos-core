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
        auto const found = m_scores.find(entry);
        if (found == m_scores.end())
            return false;
        for (GearOption const& option : options)
        {
            double worn = 0.0;
            bool known = true;
            for (WornItem const& item : option)
            {
                auto const wornScore = m_scores.find(item.entry);
                if (wornScore != m_scores.end())
                    worn += wornScore->second;
                else if (item.grade == ItemGrade::High)
                    known = false;
            }
            if (!known)
                continue;
            // A small tolerance, so that exactly a tenth more counts.
            if (found->second > worn && found->second >= worn * ClearUpgradeFactor - 1e-9)
                return true;
        }
        return false;
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
