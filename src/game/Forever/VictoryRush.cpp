/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "VictoryRush.h"
#include "Forever.h"
#include "VictoryRushRules.h"
#include "ForeverTalents.h"
#include "Log.h"
#include "Player.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "Unit.h"

using namespace Forever::VictoryRushRules;

void Forever::OnKillForVictoryRush(Unit* killer, Unit* victim)
{
    if (!victim || !IsForeverWarrior(killer))
        return;
    Player* player = killer->ToPlayer();

    Kill kill;
    kill.rulesOn = true;
    kill.killerIsWarriorPlayer = true;
    kill.killerKnowsVictoryRush = player->HasSpell(VictoryRushSpell);
    kill.killedSelf = killer == victim;
    kill.honorOrXpTarget = player->IsHonorOrXPTarget(victim);
    if (!GrantsVictorious(kill))
        return;

    player->CastSpell(player, VictoriousSpell, true);
    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Forever] Victorious: %s killed %s (level %u)",
             player->GetName(), victim->GetName(), victim->GetLevel());
}

void Forever::OnVictoriousAura(Unit* target, uint32 spellId, bool apply)
{
    if (spellId != VictoriousSpell || !target || !RulesOn())
        return;
    target->ModifyAuraState(AuraState(VictoriousAuraState), apply);
}

void Forever::VictoryRushDamage(Spell* spell, SpellEffectIndex effIdx)
{
    if (effIdx != EFFECT_INDEX_0 || spell->m_spellInfo->Id != VictoryRushSpell || !spell->m_casterUnit || !RulesOn())
        return;
    Unit* caster = spell->m_casterUnit;
    Unit* target = spell->GetUnitTarget();
    float const attackPower = Talents::AttackPowerAgainst(caster, target);
    int32 const percent = int32(spell->CalculateDamage(EFFECT_INDEX_2, target));
    spell->damage = float(Damage(int32(spell->damage), attackPower, percent));
}

bool Forever::VictoryRushDummy(Spell* spell, SpellEffectIndex effIdx)
{
    if (spell->m_spellInfo->Id != VictoryRushSpell || !RulesOn())
        return false;
    if (effIdx == EFFECT_INDEX_1 && spell->m_casterUnit && spell->m_casterUnit->IsAlive())
    {
        Unit* caster = spell->m_casterUnit;
        int32 const percent = int32(spell->CalculateDamage(EFFECT_INDEX_1, caster));
        caster->DealHeal(caster, Heal(caster->GetMaxHealth(), percent), spell->m_spellInfo);
    }
    return true;
}
