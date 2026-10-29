/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "ForeverArms.h"
#include "Forever.h"
#include "ForeverArmsRules.h"
#include "ForeverWarrior.h"
#include "Item.h"
#include "Log.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "Utilities/Random.h"

#include <algorithm>
#include <iterator>

namespace
{
    // The talent spells of forever/warrior.tsv. They keep their stock IDs (lead decision of 2026-10-29),
    // so the IDs only mean the Forever talent when the rules are on.
    uint32 const ImprovedTacticalMasteryRanks[] = { 12295, 12676, 12677, 12678, 12679 };
    uint32 const BloodthrillRanks[] = { 60101, 60102, 60103, 60104, 60105 };
    // Highest rank first.
    uint32 const WeaponmasterRanks[] = { 12815, 12814, 12813, 12812, 12281 };
    uint32 const ImprovedSlamRanks[] = { 12862, 12330 };
    uint32 const SpearingStrikeSpell = 60100;

    template <size_t N>
    bool IsOneOf(uint32 const (&ids)[N], uint32 id)
    {
        return std::find(std::begin(ids), std::end(ids), id) != std::end(ids);
    }

    // The sum of the DUMMY aura values of the talent ranks `ids` on `unit` (the talent value of the rank
    // it has learned).
    template <size_t N>
    int32 DummyTalentValue(Unit const* unit, uint32 const (&ids)[N])
    {
        int32 value = 0;
        for (Aura const* aura : unit->GetAurasByType(SPELL_AURA_DUMMY))
            if (IsOneOf(ids, aura->GetId()))
                value += aura->GetModifier()->m_amount;
        return value;
    }

    // The Weaponmaster rank spell that the Warrior has learned, or nullptr. Not its aura: the rank spells
    // keep the sword requirement of stock Sword Specialization, so the aura is missing with another weapon.
    SpellEntry const* WeaponmasterRank(Player const* warrior)
    {
        for (uint32 id : WeaponmasterRanks)
            if (warrior->HasSpell(id))
                return sSpellMgr.GetSpellEntry(id);
        return nullptr;
    }

    Forever::ArmsRules::WeaponmasterBenefit BenefitOfHand(Player const* warrior, WeaponAttackType attType)
    {
        Item const* weapon = warrior->GetWeaponForAttack(attType, true, true);
        if (!weapon)
            return Forever::ArmsRules::WeaponmasterBenefit::None;
        return Forever::ArmsRules::WeaponmasterBenefitOf(weapon->GetProto()->SubClass);
    }

    bool IsSlam(SpellEntry const* spellInfo)
    {
        return spellInfo && spellInfo->IsFitToFamily<SPELLFAMILY_WARRIOR, CF_WARRIOR_SLAM>();
    }

    bool HasImprovedSlam(Player const* warrior)
    {
        for (uint32 id : ImprovedSlamRanks)
            if (warrior->HasSpell(id))
                return true;
        return false;
    }

    bool HasOwnRend(Unit const* target, Unit const* caster)
    {
        for (Aura const* aura : target->GetAurasByType(SPELL_AURA_PERIODIC_DAMAGE))
            if (aura->GetCasterGuid() == caster->GetObjectGuid() &&
                aura->GetSpellProto()->IsFitToFamily<SPELLFAMILY_WARRIOR, CF_WARRIOR_REND>())
                return true;
        return false;
    }
}

bool Forever::KeepStanceRage(Unit* warrior, uint32 keptTenths)
{
    if (!IsForeverWarrior(warrior))
        return false;

    int32 const improved = DummyTalentValue(warrior, ImprovedTacticalMasteryRanks);
    uint32 const before = warrior->GetPower(POWER_RAGE);
    uint32 const after = ArmsRules::RageAfterStanceChange(before, keptTenths, improved);
    if (after != before)
        warrior->SetPower(POWER_RAGE, after);

    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Stance change: %s keeps %u of %u Rage tenths (base %u, Improved Tactical Mastery +%d)",
        warrior->GetName(), after, before, keptTenths, improved);
    return true;
}

bool Forever::SpearingStrike(Unit* caster, Unit* target, SpellEntry const* spellInfo, float& damage)
{
    if (!spellInfo || spellInfo->Id != SpearingStrikeSpell || !RulesOn())
        return false;
    if (!target || !IsForeverWarrior(caster))
        return true;

    bool const mounted = target->IsMounted();
    if (!ArmsRules::IsSpearingBonusTarget(target->GetCreatureType(), mounted))
        return true;

    float const before = damage;
    damage *= ArmsRules::SpearingStrikeMultiplier(true, spellInfo->CalculateSimpleValue(EFFECT_INDEX_2));
    if (mounted)
    {
        target->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
        if (target->IsMounted())
            target->Unmount();
    }

    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Spearing Strike: %s on %s (creature type %u%s), %.1f -> %.1f",
        caster->GetName(), target->GetGuidStr().c_str(), target->GetCreatureType(), mounted ? ", dismounted" : "",
        before, damage);
    return true;
}

uint32 Forever::BloodthrillOverpower(Unit* attacker, Unit* target, uint32 procFlag, uint32 procExtra, WeaponAttackType attType)
{
    if (!target || !(procFlag & (PROC_FLAG_DEAL_MELEE_SWING | PROC_FLAG_DEAL_MELEE_ABILITY)) ||
        !IsForeverWarrior(attacker))
        return 0;

    bool const landed = (procExtra & (PROC_EX_NORMAL_HIT | PROC_EX_CRITICAL_HIT)) != 0;
    if (!ArmsRules::BloodthrillCounts(attType == BASE_ATTACK, landed, HasOwnRend(target, attacker)))
        return 0;

    int32 const chance = DummyTalentValue(attacker, BloodthrillRanks);
    Player* warrior = attacker->ToPlayer();
    // `.cheat procs on` makes it proc each time, as for the procs of the core.
    if (chance <= 0 || !(warrior->HasCheatOption(PLAYER_CHEAT_ALWAYS_PROC) || roll_chance_i(chance)))
        return 0;

    warrior->AddComboPoints(target, 1);
    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Bloodthrill: %s may Overpower %s for %u ms (chance %d%%)",
        attacker->GetName(), target->GetGuidStr().c_str(), ArmsRules::BloodthrillWindowMs, chance);
    return ArmsRules::BloodthrillWindowMs;
}

float Forever::WeaponmasterCritBonus(Unit const* attacker, WeaponAttackType attType)
{
    if ((attType != BASE_ATTACK && attType != OFF_ATTACK) || !IsForeverWarrior(attacker))
        return 0.0f;

    Player const* warrior = attacker->ToPlayer();
    SpellEntry const* rank = WeaponmasterRank(warrior);
    if (!rank || BenefitOfHand(warrior, attType) != ArmsRules::WeaponmasterBenefit::Crit)
        return 0.0f;

    float const bonus = float(rank->CalculateSimpleValue(EFFECT_INDEX_2));
    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Weaponmaster crit: %s +%.0f%% (%s hand)",
        warrior->GetName(), bonus, attType == OFF_ATTACK ? "off" : "main");
    return bonus;
}

void Forever::WeaponmasterArmor(Unit const* attacker, float& armor)
{
    if (armor <= 0.0f || !IsForeverWarrior(attacker))
        return;

    // CalcArmorReducedDamage does not know the hand: the main hand weapon decides (our choice).
    Player const* warrior = attacker->ToPlayer();
    SpellEntry const* rank = WeaponmasterRank(warrior);
    if (!rank || BenefitOfHand(warrior, BASE_ATTACK) != ArmsRules::WeaponmasterBenefit::ArmorIgnore)
        return;
    float const before = armor;
    float const ignorePct = float(rank->CalculateSimpleValue(EFFECT_INDEX_1));
    armor = ArmsRules::ArmorAfterIgnore(armor, ignorePct);
    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Weaponmaster armor: %s ignores %.0f%%, %.0f -> %.0f",
        warrior->GetName(), ignorePct, before, armor);
}

bool Forever::WeaponmasterBlocksProc(Unit const* owner, SpellEntry const* auraSpell, SpellEntry const* procSpell)
{
    if (!auraSpell || !procSpell || !IsOneOf(WeaponmasterRanks, auraSpell->Id) ||
        !procSpell->IsFitToFamily<SPELLFAMILY_WARRIOR, CF_WARRIOR_SHIELD_BASH, CF_WARRIOR_SHIELD_SLAM>() ||
        !IsForeverWarrior(owner))
        return false;

    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Weaponmaster: no sword proc from spell %u of %s",
        procSpell->Id, owner->GetName());
    return true;
}

bool Forever::SwingsThroughCast(Unit const* unit)
{
    if (!IsForeverWarrior(unit))
        return false;

    Spell const* cast = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    return ArmsRules::SlamKeepsSwing(cast && IsSlam(cast->m_spellInfo), HasImprovedSlam(unit->ToPlayer()));
}

bool Forever::KeepsSwingTimer(Unit const* caster, SpellEntry const* spellInfo)
{
    if (!IsForeverWarrior(caster))
        return false;
    return ArmsRules::SlamKeepsSwing(IsSlam(spellInfo), HasImprovedSlam(caster->ToPlayer()));
}
