/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "BotLoot.h"
#include "BotMgr.h"
#include "Bag.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Packets/Item.h"
#include "Packets/Loot.h"

#include <vector>

namespace
{
    // A bot loots the corpses within this distance.
    float const CorpseSearchYards = 30.0f;
    // A bot gives up a corpse that it cannot reach or open in this many thinks.
    uint32 const MaxWalkThinks = 40;
    uint32 const MaxOpenThinks = 10;
    // The list of looted corpses is cleared at this size.
    std::size_t const MaxLootedCorpses = 200;
    std::size_t const MaxOpenRolls = 100;

    char const* RollName(Bots::LootRoll roll)
    {
        return roll == Bots::LootRoll::Need ? "need" : "greed";
    }

    struct LootableCorpseCheck
    {
        Player* bot;
        float range;

        WorldObject const& GetFocusObject() const { return *bot; }
        bool operator()(Creature* creature) const
        {
            return creature->IsDead() && bot->IsWithinDistInMap(creature, range) && bot->IsAllowedToLoot(creature);
        }
    };

    // One way to wear an item: the equipment slot and the worn items that
    // the item takes the place of.
    struct EquipOption
    {
        uint8 slot;
        Bots::GearOption replaced;
    };

    void AddWorn(Player const* bot, uint8 slot, Bots::GearOption& option)
    {
        if (Item const* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            ItemPrototype const* proto = item->GetProto();
            option.push_back({proto->ItemId, proto->Quality <= ITEM_QUALITY_NORMAL ? Bots::ItemGrade::Low : Bots::ItemGrade::High});
        }
    }

    EquipOption OptionAt(Player const* bot, uint8 slot)
    {
        EquipOption option = {slot, {}};
        AddWorn(bot, slot, option.replaced);
        return option;
    }

    bool HasTwoHander(Player const* bot)
    {
        Item const* mainHand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
        return mainHand && mainHand->GetProto()->InventoryType == INVTYPE_2HWEAPON;
    }

    std::vector<EquipOption> EquipOptionsOf(Player const* bot, ItemPrototype const* proto)
    {
        switch (proto->InventoryType)
        {
            case INVTYPE_HEAD: return {OptionAt(bot, EQUIPMENT_SLOT_HEAD)};
            case INVTYPE_NECK: return {OptionAt(bot, EQUIPMENT_SLOT_NECK)};
            case INVTYPE_SHOULDERS: return {OptionAt(bot, EQUIPMENT_SLOT_SHOULDERS)};
            case INVTYPE_BODY: return {OptionAt(bot, EQUIPMENT_SLOT_BODY)};
            case INVTYPE_CHEST:
            case INVTYPE_ROBE: return {OptionAt(bot, EQUIPMENT_SLOT_CHEST)};
            case INVTYPE_WAIST: return {OptionAt(bot, EQUIPMENT_SLOT_WAIST)};
            case INVTYPE_LEGS: return {OptionAt(bot, EQUIPMENT_SLOT_LEGS)};
            case INVTYPE_FEET: return {OptionAt(bot, EQUIPMENT_SLOT_FEET)};
            case INVTYPE_WRISTS: return {OptionAt(bot, EQUIPMENT_SLOT_WRISTS)};
            case INVTYPE_HANDS: return {OptionAt(bot, EQUIPMENT_SLOT_HANDS)};
            case INVTYPE_CLOAK: return {OptionAt(bot, EQUIPMENT_SLOT_BACK)};
            case INVTYPE_TABARD: return {OptionAt(bot, EQUIPMENT_SLOT_TABARD)};
            case INVTYPE_FINGER: return {OptionAt(bot, EQUIPMENT_SLOT_FINGER1), OptionAt(bot, EQUIPMENT_SLOT_FINGER2)};
            case INVTYPE_TRINKET: return {OptionAt(bot, EQUIPMENT_SLOT_TRINKET1), OptionAt(bot, EQUIPMENT_SLOT_TRINKET2)};
            case INVTYPE_RANGED:
            case INVTYPE_RANGEDRIGHT:
            case INVTYPE_THROWN: return {OptionAt(bot, EQUIPMENT_SLOT_RANGED)};
            case INVTYPE_2HWEAPON:
            {
                EquipOption option = OptionAt(bot, EQUIPMENT_SLOT_MAINHAND);
                AddWorn(bot, EQUIPMENT_SLOT_OFFHAND, option.replaced);
                return {option};
            }
            case INVTYPE_WEAPONMAINHAND: return {OptionAt(bot, EQUIPMENT_SLOT_MAINHAND)};
            case INVTYPE_WEAPON:
            {
                std::vector<EquipOption> options = {OptionAt(bot, EQUIPMENT_SLOT_MAINHAND)};
                // The off hand only next to a one-handed main hand.
                if (bot->CanDualWield() && !HasTwoHander(bot))
                    options.push_back(OptionAt(bot, EQUIPMENT_SLOT_OFFHAND));
                return options;
            }
            case INVTYPE_WEAPONOFFHAND:
            case INVTYPE_SHIELD:
            case INVTYPE_HOLDABLE:
            {
                // An off-hand item takes the place of a two-handed weapon.
                EquipOption option = OptionAt(bot, EQUIPMENT_SLOT_OFFHAND);
                if (HasTwoHander(bot))
                    AddWorn(bot, EQUIPMENT_SLOT_MAINHAND, option.replaced);
                return {option};
            }
            default:
                return {};
        }
    }

    std::vector<Bots::GearOption> GearOptionsOf(std::vector<EquipOption> const& options)
    {
        std::vector<Bots::GearOption> gearOptions;
        for (EquipOption const& option : options)
            gearOptions.push_back(option.replaced);
        return gearOptions;
    }

    bool CanUse(Player const* bot, ItemPrototype const* proto)
    {
        return bot->CanUseItem(proto) == EQUIP_ERR_OK;
    }

    bool IsClearUpgrade(Player const* bot, ItemPrototype const* proto)
    {
        Bots::GearScores const* scores = sBotMgr.GearScoresOf(bot);
        return scores && scores->IsClearUpgrade(proto->ItemId, GearOptionsOf(EquipOptionsOf(bot, proto)));
    }

    // An item that the bot can wear or use: the bag-full rule keeps room
    // for it.
    bool IsUsable(Player const* bot, ItemPrototype const* proto)
    {
        if (proto->Quality <= ITEM_QUALITY_POOR || !CanUse(bot, proto))
            return false;
        switch (proto->Class)
        {
            case ITEM_CLASS_WEAPON:
            case ITEM_CLASS_ARMOR:
                return !EquipOptionsOf(bot, proto).empty();
            case ITEM_CLASS_CONSUMABLE:
            case ITEM_CLASS_CONTAINER:
            case ITEM_CLASS_PROJECTILE:
            case ITEM_CLASS_QUIVER:
            case ITEM_CLASS_QUEST:
                return true;
            default:
                return false;
        }
    }

    // The items in the backpack and the bags, not the equipped ones.
    std::vector<Item*> BagItemsOf(Player const* bot)
    {
        std::vector<Item*> items;
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                items.push_back(item);
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = dynamic_cast<Bag*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot)))
                for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                    if (Item* item = bag->GetItemByPos(uint8(slot)))
                        items.push_back(item);
        return items;
    }

    void Queue(Player* bot, std::unique_ptr<ClientPacket const> packet)
    {
        bot->GetSession()->QueuePacket(std::move(packet));
    }
}

void BotLoot::OnStartRoll(Player* bot, WorldPacket const& packet)
{
    WorldPacket data(packet);
    uint64 target;
    uint32 slot, entry, randomSuffix, randomProperty, countdown;
    data >> target >> slot >> entry >> randomSuffix >> randomProperty >> countdown;

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
    if (!proto)
        return;
    Bots::LootRoll const roll = Bots::ChooseLootRoll(CanUse(bot, proto), IsClearUpgrade(bot, proto));
    if (m_rolls.size() >= MaxOpenRolls)
        m_rolls.clear(); // Rolls that ended with no SMSG_LOOT_ROLL_WON, when all passed.
    m_rolls[std::make_pair(ObjectGuid(target), slot)] = roll;

    auto vote = std::make_unique<WorldPackets::Loot::LootRoll>();
    vote->lootedTarget = ObjectGuid(target);
    vote->itemSlot = slot;
    vote->rollType = roll == Bots::LootRoll::Need ? ROLL_NEED : ROLL_GREED;
    Queue(bot, std::move(vote));
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] %s rolls %s on %u (%s)", bot->GetName(), RollName(roll), entry, proto->Name1);
}

BotLootRollWon BotLoot::OnRollWon(Player* bot, WorldPacket const& packet)
{
    WorldPacket data(packet);
    uint64 target, winner;
    uint32 randomSuffix, randomProperty;
    BotLootRollWon won;
    data >> target >> won.itemSlot >> won.itemEntry >> randomSuffix >> randomProperty >> winner >> won.rollNumber >> won.rollType;
    won.lootedTarget = ObjectGuid(target);
    won.winner = ObjectGuid(winner);

    auto const roll = m_rolls.find(std::make_pair(won.lootedTarget, won.itemSlot));
    if (roll != m_rolls.end())
    {
        won.botRolled = true;
        won.botRoll = roll->second;
        m_rolls.erase(roll);
    }
    if (won.winner == bot->GetObjectGuid())
    {
        // With full bags, the item waits in the corpse for the winner.
        m_looted.erase(won.lootedTarget);
        sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] %s wins %u with %s %u", bot->GetName(), won.itemEntry,
            won.rollType == ROLL_NEED ? "need" : "greed", uint32(won.rollNumber));
    }
    return won;
}

void BotLoot::Stop()
{
    m_step = Step::None;
    m_corpse.Clear();
    m_stepThinks = 0;
    m_lootedAll = true;
}

bool BotLoot::HasWork(Player* bot, bool mayMove) const
{
    if (m_step != Step::None)
        return true;
    return mayMove && FindCorpse(bot);
}

bool BotLoot::Think(Player* bot, bool mayMove)
{
    ++m_stepThinks;
    switch (m_step)
    {
        case Step::None:
        {
            if (!mayMove)
                break;
            Creature* corpse = FindCorpse(bot);
            if (!corpse)
                break;
            m_corpse = corpse->GetObjectGuid();
            m_step = Step::Walk;
            m_stepThinks = 0;
            bot->GetMotionMaster()->Clear();
            bot->GetMotionMaster()->MovePoint(0, corpse->GetPositionX(), corpse->GetPositionY(), corpse->GetPositionZ(), MOVE_PATHFINDING | MOVE_RUN_MODE);
            return true;
        }
        case Step::Walk:
        {
            Creature* corpse = bot->GetMap()->GetCreature(m_corpse);
            if (!corpse || !bot->IsAllowedToLoot(corpse) || m_stepThinks > MaxWalkThinks)
            {
                Finish(bot);
                break;
            }
            if (!corpse->IsWithinDistInMap(bot, bot->GetMaxLootDistance(corpse) - 1.0f, true, SizeFactor::None))
            {
                if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
                    bot->GetMotionMaster()->MovePoint(0, corpse->GetPositionX(), corpse->GetPositionY(), corpse->GetPositionZ(), MOVE_PATHFINDING | MOVE_RUN_MODE);
                return true;
            }
            bot->StopMoving();
            bot->GetMotionMaster()->MoveIdle();
            auto open = std::make_unique<WorldPackets::Loot::LootUnit>();
            open->guid = m_corpse;
            Queue(bot, std::move(open));
            m_step = Step::Open;
            m_stepThinks = 0;
            return true;
        }
        case Step::Open:
        {
            Creature* corpse = bot->GetMap()->GetCreature(m_corpse);
            if (corpse && bot->GetLootGuid() == m_corpse)
            {
                m_lootedAll = TakeLoot(bot, corpse);
                m_step = Step::Release;
                m_stepThinks = 0;
                return true;
            }
            if (!corpse || m_stepThinks > MaxOpenThinks)
            {
                Finish(bot);
                break;
            }
            return true;
        }
        case Step::Release:
        {
            if (bot->GetLootGuid() == m_corpse && m_stepThinks <= MaxOpenThinks)
                return true;
            Finish(bot);
            break;
        }
    }
    return false;
}

void BotLoot::Finish(Player* bot)
{
    if (m_looted.size() >= MaxLootedCorpses)
        m_looted.clear();
    if (m_lootedAll)
        m_looted.insert(m_corpse);
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        bot->GetMotionMaster()->MoveIdle();
    Stop();
}

Creature* BotLoot::FindCorpse(Player* bot) const
{
    std::list<Creature*> corpses;
    LootableCorpseCheck check = {bot, CorpseSearchYards};
    MaNGOS::CreatureListSearcher<LootableCorpseCheck> searcher(corpses, check);
    Cell::VisitGridObjects(bot, searcher, CorpseSearchYards);

    Creature* nearest = nullptr;
    for (Creature* corpse : corpses)
    {
        if (m_looted.count(corpse->GetObjectGuid()))
            continue;
        if (!nearest || bot->GetDistance(corpse) < bot->GetDistance(nearest))
            nearest = corpse;
    }
    return nearest;
}

bool BotLoot::TakeLoot(Player* bot, Creature* corpse)
{
    Loot* loot = &corpse->loot;
    Group const* group = bot->GetGroup();
    bool const masterLoot = group && group->GetLootMethod() == MASTER_LOOT;
    uint32 const slots = loot->GetMaxSlotInLootFor(bot->GetGUIDLow());
    bool madeRoom = false;
    bool complete = true;
    for (uint32 slot = 0; slot < slots; ++slot)
    {
        // The checks of WorldSession::HandleAutostoreLootItemOpcode, so that
        // the bot asks only for the items that it may take.
        QuestItem* questItem = nullptr;
        QuestItem* ffaItem = nullptr;
        QuestItem* conditionItem = nullptr;
        LootItem* item = loot->LootItemInSlot(slot, bot->GetGUIDLow(), &questItem, &ffaItem, &conditionItem);
        if (!item || !item->AllowedForPlayer(bot, corpse))
            continue;
        if (!questItem && item->is_blocked)
            continue; // A roll is on.
        if (masterLoot && !item->is_underthreshold && !questItem && !ffaItem)
            continue;

        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(item->itemid);
        if (!proto)
            continue;
        ItemPosCountVec dest;
        InventoryResult const fits = bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, proto->ItemId, item->count);
        if (fits == EQUIP_ERR_INVENTORY_FULL)
        {
            if (madeRoom)
            {
                // The free slot of the deleted item is taken already: the
                // bot opens the corpse again for the rest.
                complete = false;
                continue;
            }
            if (!MakeRoom(bot, proto, item->count))
                continue;
            madeRoom = true;
        }
        else if (fits != EQUIP_ERR_OK)
            continue;
        auto store = std::make_unique<WorldPackets::Loot::AutoStoreLootItem>();
        store->lootSlot = uint8(slot);
        Queue(bot, std::move(store));
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s loots %ux %u (%s)", bot->GetName(), item->count, item->itemid, proto->Name1);
    }
    if (loot->gold)
        Queue(bot, std::make_unique<NullClientPacket>(CMSG_LOOT_MONEY));
    auto release = std::make_unique<WorldPackets::Loot::LootRelease>();
    release->guid = m_corpse;
    Queue(bot, std::move(release));
    return complete;
}

bool BotLoot::MakeRoom(Player* bot, ItemPrototype const* proto, uint32 count)
{
    std::vector<Item*> const items = BagItemsOf(bot);
    std::vector<Bots::BagItem> bagItems;
    for (Item const* item : items)
        bagItems.push_back({item->GetProto()->Quality == ITEM_QUALITY_POOR, item->GetProto()->SellPrice * item->GetCount()});
    Bots::IncomingItem const incoming = {proto->SellPrice * count, IsUsable(bot, proto)};
    Bots::BagRoom const room = Bots::ChooseBagRoom(bagItems, incoming);
    if (!room.deletes)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s has full bags and leaves %u (%s)", bot->GetName(), proto->ItemId, proto->Name1);
        return false;
    }

    Item* grey = items[room.index];
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] %s has full bags and deletes %ux %u (%s) for %u (%s)", bot->GetName(),
        grey->GetCount(), grey->GetEntry(), grey->GetProto()->Name1, proto->ItemId, proto->Name1);
    bot->DestroyItem(grey->GetBagSlot(), grey->GetSlot(), true);
    return true;
}

void BotLoot::PutOnUpgrades(Player* bot)
{
    Bots::GearScores const* scores = sBotMgr.GearScoresOf(bot);
    if (!scores)
        return;
    for (Item* item : BagItemsOf(bot))
    {
        ItemPrototype const* proto = item->GetProto();
        if ((proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR) || m_triedToEquip.count(item->GetObjectGuid()))
            continue;
        if (!CanUse(bot, proto))
            continue;
        std::vector<EquipOption> const options = EquipOptionsOf(bot, proto);
        std::size_t const best = scores->BestUpgradeOption(proto->ItemId, GearOptionsOf(options));
        if (best >= options.size())
            continue;

        // One item per think: the equipped items change.
        m_triedToEquip.insert(item->GetObjectGuid());
        if (proto->InventoryType == INVTYPE_2HWEAPON)
        {
            // The core moves the off-hand item into the bags.
            auto equip = std::make_unique<WorldPackets::Item::AutoEquipItem>();
            equip->srcbag = item->GetBagSlot();
            equip->srcslot = item->GetSlot();
            Queue(bot, std::move(equip));
        }
        else
        {
            auto equip = std::make_unique<WorldPackets::Item::AutoEquipItemSlot>();
            equip->itemGuid = item->GetObjectGuid();
            equip->dstslot = options[best].slot;
            Queue(bot, std::move(equip));
        }
        sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] %s puts on %u (%s)", bot->GetName(), proto->ItemId, proto->Name1);
        return;
    }
}
