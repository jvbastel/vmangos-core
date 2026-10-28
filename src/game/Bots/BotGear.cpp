/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "BotGear.h"
#include "IO/Filesystem/FileSystem.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace
{
    bool FitsSlot(Bots::GearSlot slot, uint32 inventoryType)
    {
        switch (slot)
        {
            case Bots::GearSlot::Head: return inventoryType == INVTYPE_HEAD;
            case Bots::GearSlot::Neck: return inventoryType == INVTYPE_NECK;
            case Bots::GearSlot::Shoulders: return inventoryType == INVTYPE_SHOULDERS;
            case Bots::GearSlot::Back: return inventoryType == INVTYPE_CLOAK;
            case Bots::GearSlot::Chest: return inventoryType == INVTYPE_CHEST || inventoryType == INVTYPE_ROBE;
            case Bots::GearSlot::Shirt: return inventoryType == INVTYPE_BODY;
            case Bots::GearSlot::Tabard: return inventoryType == INVTYPE_TABARD;
            case Bots::GearSlot::Wrists: return inventoryType == INVTYPE_WRISTS;
            case Bots::GearSlot::Hands: return inventoryType == INVTYPE_HANDS;
            case Bots::GearSlot::Waist: return inventoryType == INVTYPE_WAIST;
            case Bots::GearSlot::Legs: return inventoryType == INVTYPE_LEGS;
            case Bots::GearSlot::Feet: return inventoryType == INVTYPE_FEET;
            case Bots::GearSlot::Finger: return inventoryType == INVTYPE_FINGER;
            case Bots::GearSlot::Trinket: return inventoryType == INVTYPE_TRINKET;
            case Bots::GearSlot::Ranged:
                return inventoryType == INVTYPE_RANGED || inventoryType == INVTYPE_RANGEDRIGHT ||
                    inventoryType == INVTYPE_THROWN || inventoryType == INVTYPE_RELIC;
            case Bots::GearSlot::MainHand: return inventoryType == INVTYPE_WEAPON || inventoryType == INVTYPE_WEAPONMAINHAND;
            case Bots::GearSlot::OffHand: return inventoryType == INVTYPE_WEAPON || inventoryType == INVTYPE_WEAPONOFFHAND;
            case Bots::GearSlot::TwoHand: return inventoryType == INVTYPE_2HWEAPON;
            case Bots::GearSlot::Shield: return inventoryType == INVTYPE_SHIELD;
            case Bots::GearSlot::Holdable: return inventoryType == INVTYPE_HOLDABLE;
        }
        return false;
    }

    uint8 EquipmentSlot(Bots::GearPosition position)
    {
        switch (position)
        {
            case Bots::GearPosition::Head: return EQUIPMENT_SLOT_HEAD;
            case Bots::GearPosition::Neck: return EQUIPMENT_SLOT_NECK;
            case Bots::GearPosition::Shoulders: return EQUIPMENT_SLOT_SHOULDERS;
            case Bots::GearPosition::Back: return EQUIPMENT_SLOT_BACK;
            case Bots::GearPosition::Chest: return EQUIPMENT_SLOT_CHEST;
            case Bots::GearPosition::Shirt: return EQUIPMENT_SLOT_BODY;
            case Bots::GearPosition::Tabard: return EQUIPMENT_SLOT_TABARD;
            case Bots::GearPosition::Wrists: return EQUIPMENT_SLOT_WRISTS;
            case Bots::GearPosition::Hands: return EQUIPMENT_SLOT_HANDS;
            case Bots::GearPosition::Waist: return EQUIPMENT_SLOT_WAIST;
            case Bots::GearPosition::Legs: return EQUIPMENT_SLOT_LEGS;
            case Bots::GearPosition::Feet: return EQUIPMENT_SLOT_FEET;
            case Bots::GearPosition::Finger1: return EQUIPMENT_SLOT_FINGER1;
            case Bots::GearPosition::Finger2: return EQUIPMENT_SLOT_FINGER2;
            case Bots::GearPosition::Trinket1: return EQUIPMENT_SLOT_TRINKET1;
            case Bots::GearPosition::Trinket2: return EQUIPMENT_SLOT_TRINKET2;
            case Bots::GearPosition::Ranged: return EQUIPMENT_SLOT_RANGED;
            case Bots::GearPosition::MainHand: return EQUIPMENT_SLOT_MAINHAND;
            case Bots::GearPosition::OffHand: break;
        }
        return EQUIPMENT_SLOT_OFFHAND;
    }

    bool ReadFile(std::string const& path, std::string& text)
    {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file)
            return false;
        std::ostringstream content;
        content << file.rdbuf();
        text = content.str();
        return true;
    }

    std::string const Extension = ".txt";
}

void BotGear::Load(std::string const& gearDir)
{
    m_templates.clear();
    m_scores.clear();
    std::vector<std::string> files = IO::Filesystem::GetAllFilesInFolder(gearDir, IO::Filesystem::OutputFilePath::JustFileName);
    std::sort(files.begin(), files.end());
    for (std::string const& file : files)
    {
        if (file.size() <= Extension.size() || file.compare(file.size() - Extension.size(), Extension.size(), Extension) != 0)
            continue;
        std::string const path = gearDir + "/" + file;
        std::string text;
        if (!ReadFile(path, text))
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] Cannot read the gear template %s", path.c_str());
            continue;
        }
        Bots::GearTemplateResult result = Bots::ParseGearTemplate(text);
        if (!result.ok)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] Gear template %s, %s", path.c_str(), result.error.c_str());
            continue;
        }
        Bots::GearTemplate& gear = result.gearTemplate;
        std::string const nameError = Bots::CheckGearTemplateFileName(gear, file.substr(0, file.size() - Extension.size()));
        if (!nameError.empty())
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] Gear template %s: %s", path.c_str(), nameError.c_str());
            continue;
        }

        // Each item must exist in this realm and fit its slot. An item of a
        // later content patch than the realm's is not in item_template here,
        // and the draw never takes it, so it is dropped with no message.
        std::vector<Bots::GearItem> items;
        for (Bots::GearItem const& item : gear.items)
        {
            if (item.minPatch > sWorld.GetWowPatch())
                continue;
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(item.entry);
            if (!proto)
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] Gear template %s, line %u: no item has the entry %u",
                    path.c_str(), uint32(item.line), item.entry);
            else if (!FitsSlot(item.slot, proto->InventoryType))
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] Gear template %s, line %u: the item %u (%s) does not fit the slot %s",
                    path.c_str(), uint32(item.line), item.entry, proto->Name1, Bots::GearSlotName(item.slot));
            else
                items.push_back(item);
        }
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] Gear template %s: %u of %u items in content patch %u",
            gear.name.c_str(), uint32(items.size()), uint32(gear.items.size()), uint32(sWorld.GetWowPatch()));
        gear.items = items;
        std::string const name = gear.name;
        if (gear.kind == Bots::GearTemplateKind::Generated)
            m_scores.emplace(name, Bots::GearScores(gear));
        m_templates[name] = std::move(gear);
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %u gear templates loaded from %s", uint32(m_templates.size()), gearDir.c_str());
}

Bots::GearTemplate const* BotGear::Find(std::string const& name) const
{
    auto found = m_templates.find(name);
    return found == m_templates.end() ? nullptr : &found->second;
}

Bots::GearScores const* BotGear::FindScores(std::string const& name) const
{
    auto found = m_scores.find(name);
    return found == m_scores.end() ? nullptr : &found->second;
}

Bots::Faction FactionOfTeam(uint32 team)
{
    return team == ALLIANCE ? Bots::Faction::Alliance : Bots::Faction::Horde;
}

Bots::GearDraw EquipFromGearTemplate(Player* player, Bots::GearTemplate const& gearTemplate)
{
    Bots::Faction const faction = FactionOfTeam(player->GetTeam());
    Bots::GearDraw draw = Bots::DrawGear(gearTemplate, player->GetGUIDLow(), player->GetLevel(), sWorld.GetWowPatch(), faction);

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            player->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);

    for (Bots::GearPick const& pick : draw.picks)
    {
        uint16 const position = (uint16(INVENTORY_SLOT_BAG_0) << 8) | EquipmentSlot(pick.position);
        if (!player->EquipNewItem(position, pick.entry, true))
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] %s cannot equip the item %u", player->GetName(), pick.entry);
    }
    player->SaveToDB();

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %s (GUID %u, level %u, patch %u) wears the gear template %s%s%s: %s",
        player->GetName(), player->GetGUIDLow(), uint32(player->GetLevel()), uint32(sWorld.GetWowPatch()),
        gearTemplate.name.c_str(), draw.shape.empty() ? "" : ", shape ", draw.shape.c_str(), DescribeGearDraw(draw).c_str());
    return draw;
}

std::string DescribeGearDraw(Bots::GearDraw const& draw)
{
    std::ostringstream text;
    for (Bots::GearPick const& pick : draw.picks)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(pick.entry);
        text << (text.tellp() > 0 ? "; " : "") << Bots::GearPositionName(pick.position) << " " << pick.entry
             << " " << (proto ? proto->Name1 : "?");
    }
    return text.str();
}
