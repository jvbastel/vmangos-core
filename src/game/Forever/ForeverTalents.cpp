/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "ForeverTalents.h"
#include "SpellAuras.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Unit.h"

namespace
{
    // The highest rank spell of `ranks` (rank 1 first) that `has` is true for, or nullptr.
    template <typename Has>
    SpellEntry const* TopRank(uint32 const* ranks, std::size_t count, Has has)
    {
        for (std::size_t i = count; i > 0; --i)
            if (has(ranks[i - 1]))
                return sSpellMgr.GetSpellEntry(ranks[i - 1]);
        return nullptr;
    }

    bool IsOneOf(uint32 const* ranks, std::size_t count, uint32 id)
    {
        for (std::size_t i = 0; i < count; ++i)
            if (ranks[i] == id)
                return true;
        return false;
    }
}

int32 Forever::Talents::SumDummy(Unit const* unit, uint32 const* ranks, std::size_t count)
{
    int32 value = 0;
    for (Aura const* aura : unit->GetAurasByType(SPELL_AURA_DUMMY))
        if (IsOneOf(ranks, count, aura->GetId()))
            value += aura->GetModifier()->m_amount;
    return value;
}

SpellEntry const* Forever::Talents::RankByAura(Unit const* unit, uint32 const* ranks, std::size_t count)
{
    return TopRank(ranks, count, [unit](uint32 id) { return unit->HasAura(id); });
}

SpellEntry const* Forever::Talents::RankBySpell(Unit const* unit, uint32 const* ranks, std::size_t count)
{
    return TopRank(ranks, count, [unit](uint32 id) { return unit->HasSpell(id); });
}

int32 Forever::Talents::RankEffectValue(SpellEntry const* rank, SpellEffectIndex eff)
{
    return rank ? rank->CalculateSimpleValue(eff) : 0;
}

float Forever::Talents::AttackPowerAgainst(Unit const* caster, Unit const* target)
{
    float attackPower = caster->GetTotalAttackPowerValue(BASE_ATTACK);
    if (target)
        attackPower += caster->GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_MELEE_ATTACK_POWER_VERSUS, target->GetCreatureTypeMask());
    return attackPower;
}
