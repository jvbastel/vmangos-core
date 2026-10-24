/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/


#include "BotLevelRoutine.h"
#include "Bag.h"
#include "Chat.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellMgr.h"

#include <fstream>
#include <set>
#include <sstream>

namespace
{
    char const* ClassFileName(uint8 botClass)
    {
        switch (botClass)
        {
            case CLASS_WARRIOR: return "warrior";
            case CLASS_PALADIN: return "paladin";
            case CLASS_HUNTER:  return "hunter";
            case CLASS_ROGUE:   return "rogue";
            case CLASS_PRIEST:  return "priest";
            case CLASS_SHAMAN:  return "shaman";
            case CLASS_MAGE:    return "mage";
            case CLASS_WARLOCK: return "warlock";
            case CLASS_DRUID:   return "druid";
            default:            return nullptr;
        }
    }

    // Gives the protected trainer helper of `.learn all_trainer` to the
    // level routine, so that both teach trainer spells the same way.
    class TrainerLearner : public ChatHandler
    {
        public:
            explicit TrainerLearner(Player* player) : ChatHandler(player) {}
            using ChatHandler::HandleLearnTrainerHelper;
    };

    uint8 const WeaponSlots[] = {EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND, EQUIPMENT_SLOT_RANGED};
}

void BotLevelRoutine::Load(std::string const& dataDir)
{
    m_classSpells.clear();
    m_trainerSpells.clear();
    uint32 count = 0;
    for (uint8 botClass = CLASS_WARRIOR; botClass < MAX_CLASSES; ++botClass)
    {
        char const* name = ClassFileName(botClass);
        if (!name)
            continue;
        std::string const path = dataDir + "/spells/" + name + ".txt";
        std::ifstream file(path, std::ios::binary);
        if (!file)
            continue;
        std::ostringstream text;
        text << file.rdbuf();
        Bots::SpellFileResult result = Bots::ParseSpellFile(text.str());
        if (!result.ok)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] %s: %s", path.c_str(), result.error.c_str());
            continue;
        }
        std::vector<Bots::ClassSpell>& spells = m_classSpells[botClass];
        for (Bots::ClassSpell const& spell : result.spells)
        {
            if (!sSpellMgr.GetSpellEntry(spell.spellId))
            {
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] %s: spell %u does not exist", path.c_str(), spell.spellId);
                continue;
            }
            spells.push_back(spell);
            ++count;
        }
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %u spells of %u classes loaded from %s/spells",
        count, uint32(m_classSpells.size()), dataDir.c_str());
}

void BotLevelRoutine::Apply(Player* bot) const
{
    std::size_t const knownBefore = bot->GetSpellMap().size();
    // The quest spells first: a trainer teaches only the higher ranks of
    // some of them, for example of Sunder Armor.
    TeachQuestSpells(bot);
    TeachTrainerSpells(bot);
    TeachWeaponSkills(bot);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %s at level %u: the level routine taught %u spells",
        bot->GetName(), bot->GetLevel(), uint32(bot->GetSpellMap().size() - knownBefore));
    if (Pet* pet = bot->GetPet())
        TeachPetSpells(bot, pet);
}

void BotLevelRoutine::TeachQuestSpells(Player* bot) const
{
    auto const spells = m_classSpells.find(bot->GetClass());
    if (spells == m_classSpells.end())
        return;
    for (uint32 spellId : Bots::SpellsUpToLevel(spells->second, Bots::SpellKind::Quest, bot->GetLevel()))
    {
        // The priest file has the racial spells of all races.
        if (!bot->HasSpell(spellId) && bot->IsSpellFitByClassAndRace(spellId))
            bot->LearnSpell(spellId, false);
    }
}

std::vector<TrainerSpellData const*> const& BotLevelRoutine::ClassTrainerSpells(uint8 botClass) const
{
    auto const known = m_trainerSpells.find(botClass);
    if (known != m_trainerSpells.end())
        return known->second;

    // The trainers of `.learn all_trainer`, but only the class trainers of
    // the class: no profession, riding, or pet trainer.
    std::vector<TrainerSpellData const*>& lists = m_trainerSpells[botClass];
    std::set<uint32> templates;
    for (auto const& itr : sObjectMgr.GetCreatureInfoMap())
    {
        CreatureInfo const* info = itr.second.get();
        if (!info || !(info->npc_flags & UNIT_NPC_FLAG_TRAINER)
            || info->trainer_type != TRAINER_TYPE_CLASS || info->trainer_class != botClass)
            continue;
        if (TrainerSpellData const* spells = sObjectMgr.GetNpcTrainerSpells(itr.first))
            lists.push_back(spells);
        if (info->trainer_id && templates.insert(info->trainer_id).second)
            if (TrainerSpellData const* spells = sObjectMgr.GetNpcTrainerTemplateSpells(info->trainer_id))
                lists.push_back(spells);
    }
    return lists;
}

void BotLevelRoutine::TeachTrainerSpells(Player* bot) const
{
    // The helper teaches the spells that the trainer shows green: the
    // required level is reached, and the lower rank is known.
    TrainerLearner learner(bot);
    for (TrainerSpellData const* spells : ClassTrainerSpells(bot->GetClass()))
        learner.HandleLearnTrainerHelper(bot, spells);
}

void BotLevelRoutine::TeachWeaponSkills(Player* bot) const
{
    // The weapons that the bot wears or carries in its bags. The core moves a
    // worn weapon without its skill out of the slot at login, so a weapon of
    // the gear template or of the loot can wait in a bag.
    std::vector<Item const*> items;
    for (uint8 slot : WeaponSlots)
        items.push_back(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        items.push_back(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        if (Bag const* container = dynamic_cast<Bag const*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
            for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                items.push_back(container->GetItemByPos(uint8(slot)));

    for (Item const* item : items)
    {
        if (!item || item->GetProto()->Class != ITEM_CLASS_WEAPON)
            continue;
        // Only the weapon skills of the class, for example no axes for a priest.
        uint32 const proficiency = item->GetProto()->GetProficiencySpell();
        if (proficiency && !bot->HasSpell(proficiency) && bot->IsSpellFitByClassAndRace(proficiency))
            bot->LearnSpell(proficiency, false);
    }
    // Every skill but professions and riding to the maximum of the level,
    // which is 5 times L for weapon skills.
    bot->UpdateSkillsToMaxSkillsForLevel();
}

void BotLevelRoutine::TeachPetSpells(Player* bot, Pet* pet) const
{
    auto const spells = m_classSpells.find(bot->GetClass());
    if (spells == m_classSpells.end())
        return;
    // Lower ranks first. Pet::LearnSpell replaces a lower rank and ignores a
    // known spell or a lower rank of a known spell.
    uint32 taught = 0;
    for (uint32 spellId : Bots::SpellsUpToLevel(spells->second, Bots::SpellKind::Pet, bot->GetLevel()))
        if (pet->CanLearnPetSpell(spellId) && !pet->HasSpell(spellId) && pet->LearnSpell(spellId))
            ++taught;
    if (taught)
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %s taught %u spells to its pet %s", bot->GetName(), taught, pet->GetName());
}
