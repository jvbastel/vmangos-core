/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "ForeverArms.h"
#include "Forever.h"
#include "ForeverArmsRules.h"
#include "ForeverTalents.h"
#include "Item.h"
#include "Log.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "Utilities/Random.h"

namespace
{
    using namespace Forever::SpellIds;
    using Forever::Talents::RankBySpell;
    using Forever::Talents::SumDummy;

    // The talent spells keep their stock IDs (lead decision of 2026-10-29), so the IDs only mean the
    // Forever talent when the rules are on. The Weaponmaster rank is the spell that the Warrior has
    // learned, not its aura: the rank spells keep the sword requirement of stock Sword Specialization, so
    // the aura is missing with another weapon.

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
        return KnownRank(ImprovedSlamRanks, [warrior](uint32 id) { return warrior->HasSpell(id); }) != 0;
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

bool Forever::ApplyStanceRage(Unit* warrior, uint32 keptTenths)
{
    if (!IsForeverWarrior(warrior))
        return false;

    int32 const improved = SumDummy(warrior, ImprovedTacticalMasteryRanks);
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
    bool dismounted = false;
    if (mounted)
    {
        target->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
        if (target->IsMounted())
            target->Unmount();
        dismounted = !target->IsMounted();
    }

    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Spearing Strike: %s on %s (creature type %u%s), %.1f -> %.1f",
        caster->GetName(), target->GetGuidStr().c_str(), target->GetCreatureType(), dismounted ? ", dismounted" : (mounted ? ", still mounted" : ""),
        before, damage);
    return true;
}

uint32 Forever::BloodthrillOverpower(Unit* attacker, Unit* target, uint32 procFlag, uint32 procExtra, WeaponAttackType attType,
    SpellEntry const* procSpell)
{
    if (!target || !(procFlag & (PROC_FLAG_DEAL_MELEE_SWING | PROC_FLAG_DEAL_MELEE_ABILITY)) ||
        !IsForeverWarrior(attacker))
        return 0;
    // The hit that applies Rend does not count (the target is not yet afflicted), and Overpower does not
    // re-open itself.
    if (procSpell && procSpell->IsFitToFamily<SPELLFAMILY_WARRIOR, CF_WARRIOR_REND, CF_WARRIOR_OVERPOWER>())
        return 0;

    bool const landed = (procExtra & (PROC_EX_NORMAL_HIT | PROC_EX_CRITICAL_HIT)) != 0;
    if (!ArmsRules::BloodthrillCounts(attType == BASE_ATTACK, landed, HasOwnRend(target, attacker)))
        return 0;

    int32 const chance = SumDummy(attacker, BloodthrillRanks);
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
    SpellEntry const* rank = RankBySpell(warrior, WeaponmasterRanks);
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
    SpellEntry const* rank = RankBySpell(warrior, WeaponmasterRanks);
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
    if (!auraSpell || !procSpell || !Contains(WeaponmasterRanks, auraSpell->Id) ||
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
