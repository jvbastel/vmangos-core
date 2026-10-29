/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "ForeverWarrior.h"
#include "Forever.h"
#include "ForeverWarriorRules.h"
#include "Creature.h"
#include "DamageStructs.h"
#include "Item.h"
#include "Log.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellEntry.h"
#include "Unit.h"
#include "World.h"
#include "Utilities/Random.h"

namespace
{
    // Deep Wound, the bleed of the Deep Wounds talent (stock spell, family 0).
    uint32 const DeepWoundSpell = 12721;

    bool IsWarriorDot(SpellEntry const* spellProto)
    {
        return spellProto && (spellProto->Id == DeepWoundSpell ||
            spellProto->IsFitToFamily<SPELLFAMILY_WARRIOR, CF_WARRIOR_REND>());
    }

    // The weapon of a hand that the Warrior can swing (not broken, usable), or nullptr (unarmed).
    Item* SwingWeapon(Player const* warrior, WeaponAttackType attackType)
    {
        return warrior->GetWeaponForAttack(attackType, true, true);
    }
}

bool Forever::RewardSwingRage(Unit* attacker, CleanDamage const* cleanDamage)
{
    if (!IsForeverWarrior(attacker))
        return false;

    // Only a melee swing gives rage, and a swing has a CleanDamage. Direct damage without one (a GM
    // command, an instant kill effect) gives none.
    if (!cleanDamage)
        return true;

    Player* warrior = attacker->ToPlayer();
    bool const offHand = cleanDamage->attackType == OFF_ATTACK;
    bool const crit = cleanDamage->hitOutCome == MELEE_HIT_CRIT;

    uint32 speedMs = WarriorRules::UnarmedSpeedMs;
    bool twoHand = false;
    if (Item const* weapon = SwingWeapon(warrior, offHand ? OFF_ATTACK : BASE_ATTACK))
    {
        speedMs = weapon->GetProto()->Delay;
        twoHand = weapon->GetProto()->InventoryType == INVTYPE_2HWEAPON;
    }

    float const rage = WarriorRules::SwingRage(speedMs, twoHand, offHand, crit) *
        sWorld.getConfig(CONFIG_FLOAT_RATE_POWER_RAGE_INCOME);
    uint32 const tenths = uint32(rage * 10);
    warrior->ModifyPower(POWER_RAGE, tenths);

    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Warrior rage: %s +%u tenths (weapon %u ms, %s, %s, %s)",
        warrior->GetName(), tenths, speedMs, twoHand ? "two-hand" : "one-hand",
        offHand ? "off hand" : "main hand", crit ? "crit" : "hit");
    return true;
}

bool Forever::NoRageForAvoidedSwing(Unit const* attacker)
{
    // Forever: dodges and parries give no rage (stock: 75% of the damage the swing would have done).
    return IsForeverWarrior(attacker);
}

bool Forever::RollDotCrit(Unit* caster, Unit* target, SpellEntry const* spellProto, float& damage)
{
    if (!target || !IsWarriorDot(spellProto) || !IsForeverWarrior(caster) || damage <= 0.0f)
        return false;

    Player* warrior = caster->ToPlayer();
    // The melee crit chance of the main hand and the crit chance talents of the spell. Unit::IsSpellCrit
    // cannot do it: an aura-only spell such as Rend never crits there (SpellEntry::CanCrit).
    float chance = warrior->HasCheatOption(PLAYER_CHEAT_ALWAYS_CRIT) ? 100.0f :
        warrior->GetUnitCriticalChance(BASE_ATTACK, target);
    warrior->ApplySpellMod(spellProto->Id, SPELLMOD_CRITICAL_CHANCE, chance);
    if (!roll_chance_f(chance))
        return false;

    // Impale and other crit damage talents raise the crit bonus (100% of the normal damage for melee) by a
    // percent, as in SpellCaster::SpellCriticalDamageBonus. ApplySpellMod skips percent mods on a base of 0,
    // so start from the 100% bonus. Then the crit damage bonus versus a creature type.
    float critBonusPct = 100.0f;
    warrior->ApplySpellMod(spellProto->Id, SPELLMOD_CRIT_DAMAGE_BONUS, critBonusPct);
    float const normal = damage;
    damage = WarriorRules::DotCritDamage(normal, critBonusPct - 100.0f) *
        warrior->GetTotalAuraMultiplierByMiscMask(SPELL_AURA_MOD_CRIT_PERCENT_VERSUS, target->GetCreatureTypeMask());

    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Warrior DoT crit: %s spell %u on %s, %.1f -> %.1f",
        warrior->GetName(), spellProto->Id, target->GetGuidStr().c_str(), normal, damage);
    return true;
}

void Forever::TapOnRendApply(Unit* caster, Unit* target, SpellEntry const* spellProto)
{
    if (!target || !spellProto || !spellProto->IsFitToFamily<SPELLFAMILY_WARRIOR, CF_WARRIOR_REND>() ||
        !IsForeverWarrior(caster))
        return;

    // Forever: Rend taps the target at once, not at its first tick. The same rule as the first damage in
    // Unit::DealDamage: a creature that is not a pet and has no loot recipient yet.
    Creature* creature = target->ToCreature();
    if (creature && !creature->IsPet() && !creature->HasLootRecipient())
        creature->SetLootRecipient(caster);
}

void Forever::DeepWoundsWeaponDamage(Unit* caster, float& damage)
{
    if (!IsForeverWarrior(caster))
        return;

    Player* warrior = caster->ToPlayer();
    // The same hand as the stock code: the off hand when it swung last.
    WeaponAttackType const attackType = warrior->HaveOffhandWeapon() &&
        warrior->GetAttackTimer(BASE_ATTACK) > warrior->GetAttackTimer(OFF_ATTACK) ? OFF_ATTACK : BASE_ATTACK;
    float minDamage = BASE_MINDAMAGE;
    float maxDamage = BASE_MAXDAMAGE;
    if (Item const* weapon = SwingWeapon(warrior, attackType))
    {
        minDamage = weapon->GetProto()->Damage[0].DamageMin;
        maxDamage = weapon->GetProto()->Damage[0].DamageMax;
    }
    // The talent percent comes after this hook (x0.2/0.4/0.6), so pass 100.
    damage = WarriorRules::DeepWoundsDamage(minDamage, maxDamage, 100.0f);
}

void Forever::DeepWoundsRoll(Unit* caster, Unit* target, int32& tickDamage)
{
    if (!target || !IsForeverWarrior(caster))
        return;

    SpellAuraHolder const* old = target->GetSpellAuraHolder(DeepWoundSpell, caster->GetObjectGuid());
    Aura const* oldAura = old ? old->GetAuraByEffectIndex(EFFECT_INDEX_0) : nullptr;
    if (!oldAura)
        return;

    uint32 const maxTicks = oldAura->GetAuraMaxTicks();
    uint32 const done = oldAura->GetAuraTicks();
    uint32 const left = maxTicks > done ? maxTicks - done : 0;
    // The old tick damage already holds the damage bonuses of its apply; the new bleed applies them
    // again on the rolled part. A small overcount that we accept (no published rule).
    float const newDamage = float(tickDamage) * WarriorRules::DeepWoundTicks;
    tickDamage = int32(WarriorRules::DeepWoundsRolledTick(newDamage, oldAura->GetModifier()->m_amount, left));
}
