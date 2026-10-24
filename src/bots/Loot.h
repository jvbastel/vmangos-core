#ifndef WOWAI_BOTS_LOOT_H
#define WOWAI_BOTS_LOOT_H

#include "GearTemplate.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

// The loot decisions of a bot: need or greed, clear upgrades, and the
// bag-full rule. See "8. Loot, death, and rest: Loot" in the version 1 build
// spec. The game layer collects the facts and carries out the decision.
namespace Bots
{
    enum class LootRoll
    {
        Need,
        Greed,
    };

    // The roll of a bot on SMSG_LOOT_START_ROLL. `canUse`: the bot can use
    // the item now (class, race, proficiency, and level). `clearUpgrade`: see
    // GearScores::IsClearUpgrade. A bot rolls need only on a clear upgrade
    // that it can use, and greed on everything else. It never passes.
    LootRoll ChooseLootRoll(bool canUse, bool clearUpgrade);

    // The quality of a worn item, as far as the upgrade rule needs it.
    enum class ItemGrade
    {
        Low,  // Grey or white.
        High, // Green or better.
    };

    struct WornItem
    {
        std::uint32_t entry;
        ItemGrade grade;
    };

    // One way to wear a new item: the worn items that it takes the place of.
    // Empty when the item goes into an empty position. A two-handed weapon
    // replaces the main hand and the off hand; a ring has two options, one
    // for each finger.
    using GearOption = std::vector<WornItem>;

    // An item must score at least this many times the score of the items
    // that it replaces to be a clear upgrade.
    extern double const ClearUpgradeFactor;

    // The scores of the generated gear template of a class and role: the
    // stat weights of the role, for every item that the template lists.
    class GearScores
    {
        public:
            explicit GearScores(GearTemplate const& gearTemplate);

            // True when the item is in the template, and for at least one
            // option, it scores more than the replaced items and at least
            // ClearUpgradeFactor times their score. A worn item that is not
            // in the template counts as 0 when it is grey or white (starting
            // gear). A worn green or better item that is not in the template
            // has an unknown score, so that option is never a clear upgrade.
            bool IsClearUpgrade(std::uint32_t entry, std::vector<GearOption> const& options) const;

            // The index of the option where the item is a clear upgrade with
            // the largest gain in score (the first one on a tie), or
            // `options.size()` when the item is no clear upgrade.
            std::size_t BestUpgradeOption(std::uint32_t entry, std::vector<GearOption> const& options) const;

        private:
            std::map<std::uint32_t, double> m_scores;
    };

    // An item in the bags of a bot.
    struct BagItem
    {
        bool grey;
        std::uint32_t value; // The vendor sell price of the stack, in copper.
    };

    // An item that does not fit into the full bags.
    struct IncomingItem
    {
        std::uint32_t value; // The vendor sell price of the stack, in copper.
        bool usable;         // The bot can wear or use the item.
    };

    struct BagRoom
    {
        bool deletes;      // The bot deletes one bag item to make room.
        std::size_t index; // The bag item to delete, when it deletes one.
    };

    // The bag-full rule: a bot deletes its cheapest grey item for an item
    // that is worth more or that it can use. On equal values, the first grey
    // item goes. With no grey item, the bot deletes nothing.
    BagRoom ChooseBagRoom(std::vector<BagItem> const& bagItems, IncomingItem const& incoming);
}

#endif
