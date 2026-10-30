/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "ForeverFury.h"
#include "Forever.h"
#include "ForeverFuryRules.h"
#include "ForeverTalents.h"
#include "DamageStructs.h"
#include "Log.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "Utilities/Random.h"

#include <vector>

using namespace Forever::FuryRules;

namespace
{
    using Forever::Talents::RankByAura;
    using Forever::Talents::RankEffectValue;

    // A self buff of the Forever data. False when the Forever spell row is missing (no Forever data).
    bool CastBuff(Unit* unit, uint32 buffId)
    {
        if (!sSpellMgr.GetSpellEntry(buffId))
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Forever] Fury: spell %u is missing (Forever data not loaded?).", buffId);
            return false;
        }
        unit->CastSpell(unit, buffId, true);
        return true;
    }

    // The harmful auras that slow or root: a root or snare mechanic (as Improved Sprint removes them), or
    // a slow or root aura with no mechanic (Hamstring and Frost Nova have none in the 1.12 data).
    void RemoveMovementImpairing(Unit* unit)
    {
        std::vector<uint32> spells;
        for (auto const& itr : unit->GetSpellAuraHolderMap())
        {
            SpellAuraHolder const* holder = itr.second;
            if (holder->IsPositive() || holder->IsPassive() || holder->GetSpellProto()->HasAttribute(SPELL_ATTR_NO_IMMUNITIES))
                continue;
            if (holder->HasMechanicMask(IMMUNE_TO_ROOT_AND_SNARE_MASK) || holder->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) ||
                holder->HasAuraType(SPELL_AURA_MOD_ROOT))
                spells.push_back(holder->GetId());
        }
        for (uint32 spellId : spells)
            unit->RemoveAurasDueToSpell(spellId);
    }

    bool IsMeleeAttack(DamageEffectType damagetype, CleanDamage const* cleanDamage, SpellEntry const* spellProto)
    {
        if (damagetype == DIRECT_DAMAGE)
            return cleanDamage && !spellProto;      // a swing
        return damagetype == SPELL_DIRECT_DAMAGE && spellProto && spellProto->DmgClass == SPELL_DAMAGE_CLASS_MELEE;
    }
}

void Forever::StartLingeringRage(Unit* unit)
{
    if (!IsForeverWarrior(unit) || !unit->IsAlive() || !unit->IsInWorld() || !unit->GetPower(POWER_RAGE))
        return;

    // Lingering Rage: effect 1 of the rank is the delay in ms (2000-10000). The buff stops the decay
    // (RageDecays); its spell lasts 10 sec, so set the duration of the rank.
    int32 const delayMs = RankEffectValue(RankByAura(unit, LingeringRageRanks), EFFECT_INDEX_0);
    if (delayMs <= 0 || !CastBuff(unit, LingeringRageBuff))
        return;
    if (SpellAuraHolder* holder = unit->GetSpellAuraHolder(LingeringRageBuff))
    {
        holder->SetAuraMaxDuration(delayMs);
        holder->SetAuraDuration(delayMs);
        holder->UpdateAuraDuration();
    }
    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Lingering Rage: %s keeps its rage for %d ms.", unit->GetName(), delayMs);
}

bool Forever::RageDecays(Player const* player)
{
    bool const running = IsForeverWarrior(player) && player->HasAura(LingeringRageBuff);
    return FuryRules::RageDecays(false, running);
}

float Forever::OffHandHitBonus(Unit const* attacker, WeaponAttackType attType)
{
    if (attType != OFF_ATTACK || !IsForeverWarrior(attacker))
        return 0.0f;
    return float(FuryRules::OffHandHitBonus(true, RankEffectValue(RankByAura(attacker, FuriousPrecisionRanks), EFFECT_INDEX_0)));
}

void Forever::OnSpellCast(Unit* caster, SpellEntry const* spellInfo)
{
    if (!spellInfo || !IsForeverWarrior(caster))
        return;

    // Gore Drinker: the buff gives the next 3 melee attacks the heal (OnMeleeDamageDealt).
    if (IsGoreDrinkerTrigger(spellInfo->Id) && RankByAura(caster, GoreDrinkerRanks) && CastBuff(caster, GoreDrinkerBuff))
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Gore Drinker: %s gets the buff from spell %u.", caster->GetName(), spellInfo->Id);

    // Improved Berserker Rage: effect 2 of the rank is the chance (50/100). Its rage part is stock (23690/1).
    if (spellInfo->Id == BerserkerRageSpell)
    {
        int32 const chance = RankEffectValue(RankByAura(caster, ImprovedBerserkerRageRanks), EFFECT_INDEX_1);
        if (RemovesSnares(chance, rand_chance_f()))
        {
            RemoveMovementImpairing(caster);
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Improved Berserker Rage: %s removes its snares (%d%%).", caster->GetName(), chance);
        }
    }
}

void Forever::OnMeleeDamageDealt(Unit* attacker, Unit* victim, DamageEffectType damagetype,
    CleanDamage const* cleanDamage, SpellEntry const* spellProto)
{
    if (attacker == victim || !IsMeleeAttack(damagetype, cleanDamage, spellProto) || !IsForeverWarrior(attacker))
        return;

    SpellAuraHolder* buff = attacker->GetSpellAuraHolder(GoreDrinkerBuff);
    if (!buff || buff->IsDeleted())
        return;

    // Effect 2 of the rank: tenths of a percent of the maximum health (5 = 0.5%, 10 = 1%).
    uint32 const heal = GoreDrinkerHeal(attacker->GetMaxHealth(), RankEffectValue(RankByAura(attacker, GoreDrinkerRanks), EFFECT_INDEX_1));
    if (heal)
        attacker->DealHeal(attacker, heal, buff->GetSpellProto());
    if (buff->DropAuraCharge())
        attacker->RemoveAurasDueToSpell(GoreDrinkerBuff);   // by ID, as the stock proc charges do
}

void Forever::OnDamageTaken(Unit* attacker, Unit* victim, uint32 damage, DamageEffectType damagetype)
{
    if (attacker == victim || damagetype == DOT || damagetype == SELF_DAMAGE || !IsForeverWarrior(victim))
        return;

    // Blood Craze: effect 2 of the rank is the percent (20), effect 1 the stock heal trigger (16488/90/91).
    // The stock proc on a crit taken stays; a crit of more than 20% refreshes the same heal.
    SpellEntry const* rank = RankByAura(victim, BloodCrazeRanks);
    if (!rank || !BloodCrazeTriggers(damage, victim->GetMaxHealth(), RankEffectValue(rank, EFFECT_INDEX_1)))
        return;
    victim->CastSpell(victim, rank->EffectTriggerSpell[EFFECT_INDEX_0], true);
    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Blood Craze: %s took %u of %u health in one hit.",
        victim->GetName(), damage, victim->GetMaxHealth());
}
