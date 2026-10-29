/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "ForeverProtection.h"
#include "ForeverProtectionRules.h"
#include "ForeverWarrior.h"
#include "Item.h"
#include "Log.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "Utilities/Random.h"

namespace
{
    using namespace Forever::ProtectionRules;

    bool IsUsableShield(Item const* item)
    {
        return item && !item->IsBroken() && item->GetProto()->Class == ITEM_CLASS_ARMOR &&
            item->GetProto()->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD;
    }

    bool HasUsableShield(Player const* player)
    {
        return IsUsableShield(player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND));
    }

    // The sum of effect 1 of the unit's dummy auras whose spell passes `isTalentSpell` (the talent value of
    // the rank that the unit has: Master of Defense chance, Iron Will percent).
    int32 TalentValue(Unit const* unit, bool (*isTalentSpell)(uint32_t))
    {
        int32 total = 0;
        for (Aura const* aura : unit->GetAurasByType(SPELL_AURA_DUMMY))
            if (isTalentSpell(aura->GetId()))
                total += aura->GetModifier()->m_amount;
        return total;
    }

    // Casts or removes each Defiance and Bastion rank that the Warrior knows, so that its aura is on only
    // with `shield` and in the stance of the spell. The aura reaches the client with the normal aura update;
    // a passive aura is not saved, so the next login builds it again (UpdateShieldTalents).
    void SetShieldTalents(Player* warrior, bool shield)
    {
        for (uint32 spellId : ShieldTalentSpells)
        {
            if (!warrior->HasSpell(spellId))
                continue;
            SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(spellId);
            if (!spellInfo)
                continue;

            bool const stanceFits = !spellInfo->Stances ||
                spellInfo->IsNeedCastSpellAtFormApply(warrior->GetShapeshiftForm());
            bool const active = ShieldTalentActive(shield, stanceFits);
            bool const has = warrior->HasAura(spellId);
            if (active == has)
                continue;

            if (active)
                warrior->CastSpell(warrior, spellId, true);
            else
                warrior->RemoveAurasDueToSpell(spellId);
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Shield talent: %s spell %u %s (%s)", warrior->GetName(),
                spellId, active ? "on" : "off", shield ? "shield" : "no shield");
        }
    }
}

bool Forever::ShieldTalentBlocked(Unit const* caster, SpellEntry const* spellInfo)
{
    if (!spellInfo || !IsShieldTalentSpell(spellInfo->Id) || !IsForeverWarrior(caster))
        return false;
    return !HasUsableShield(caster->ToPlayer());
}

void Forever::OnOffHandChanged(Player* player, Item const* item, bool apply)
{
    if (!IsForeverWarrior(player))
        return;
    // At unequip or break the item is still in the slot, so the slot cannot tell.
    SetShieldTalents(player, apply && IsUsableShield(item));
}

void Forever::UpdateShieldTalents(Player* player)
{
    if (!IsForeverWarrior(player))
        return;
    SetShieldTalents(player, HasUsableShield(player));
}

void Forever::OnDodgeOrParry(Unit* victim, uint32 procExtra)
{
    bool const avoided = (procExtra & (PROC_EX_DODGE | PROC_EX_PARRY)) != 0;
    if (!avoided || !IsForeverWarrior(victim))
        return;

    Player* warrior = victim->ToPlayer();
    int32 const chance = TalentValue(warrior, IsMasterOfDefenseSpell);
    if (!MasterOfDefenseCanProc(avoided, HasUsableShield(warrior), chance) || !roll_chance_i(chance))
        return;

    // 23602 (forever/warrior.tsv, base row Shield Specialization): an energize of 5 Rage. The client sees
    // it in the combat log (SMSG_SPELLENERGIZELOG) and in the Rage bar.
    warrior->CastSpell(warrior, MasterOfDefenseRageSpell, true);
    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Master of Defense: %s %s, spell %u (chance %d%%)",
        warrior->GetName(), (procExtra & PROC_EX_DODGE) ? "dodge" : "parry", MasterOfDefenseRageSpell, chance);
}

void Forever::ApplyIronWill(Unit const* target, SpellAuraHolder const* holder, int32& durationMs)
{
    if (durationMs <= 0 || !holder || !IsForeverWarrior(target))
        return;
    SpellEntry const* spellInfo = holder->GetSpellProto();
    if (!spellInfo->HasAura(SPELL_AURA_MOD_STUN) && !spellInfo->HasAura(SPELL_AURA_MOD_FEAR))
        return;
    int32 const pct = TalentValue(target, IsIronWillSpell);
    if (pct <= 0)
        return;

    int32 const shorter = IronWillDuration(durationMs, pct);
    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Iron Will: %s spell %u %d -> %d ms (%d%%)",
        target->GetName(), spellInfo->Id, durationMs, shorter, pct);
    durationMs = shorter;
}
