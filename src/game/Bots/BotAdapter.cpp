/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "BotAdapter.h"
#include "BotLoot.h"
#include "Log.h"
#include "MotionMaster.h"
#include "spline/MoveSpline.h"
#include "CreatureAI.h"
#include "Pet.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "TargetedMovementGenerator.h"
#include "Utilities/Random.h"

#include <limits>

namespace
{
    // A following bot keeps this distance to the unit, at a random angle.
    float const MinFollowYards = 3.0f;
    float const MaxFollowYards = 6.0f;
    // A bot behind its target stays this far from it, like the melee party
    // bots of CombatBotBaseAI.
    float const BehindYards = 1.0f;

    Bots::Guid GuidOf(Unit const* unit)
    {
        return unit ? unit->GetObjectGuid().GetRawValue() : Bots::NoGuid;
    }
}

Unit* BotAdapter::Find(Bots::Guid unit) const
{
    if (unit == Bots::NoGuid)
        return nullptr;
    if (unit == GetSelf())
        return me;
    Unit* found = ObjectAccessor::GetUnit(*me, ObjectGuid(unit));
    return found && found->IsInWorld() && found->IsInMap(me) ? found : nullptr;
}

Bots::Guid BotAdapter::GetSelf() const
{
    return GuidOf(me);
}

Bots::Health BotAdapter::GetHealth(Bots::Guid unit) const
{
    Unit const* found = Find(unit);
    return found ? Bots::Health{found->GetHealth(), found->GetMaxHealth()} : Bots::Health{0, 0};
}

Bots::SpellId BotAdapter::GetCurrentCast(Bots::Guid unit) const
{
    Unit const* found = Find(unit);
    if (!found)
        return Bots::NoSpell;
    for (CurrentSpellTypes type : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL})
        if (Spell const* spell = found->GetCurrentSpell(type))
            return spell->m_spellInfo->Id;
    return Bots::NoSpell;
}

Bots::Power BotAdapter::GetPower(Bots::Guid unit) const
{
    Unit const* found = Find(unit);
    if (!found)
        return {0, 0};
    Powers const power = found->GetPowerType();
    return {found->GetPower(power), found->GetMaxPower(power)};
}

float BotAdapter::GetDistance(Bots::Guid unit) const
{
    Unit const* found = Find(unit);
    return found ? me->GetDistance(found) : std::numeric_limits<float>::max();
}

bool BotAdapter::IsInCombat(Bots::Guid unit) const
{
    Unit const* found = Find(unit);
    return found && found->IsInCombat();
}

Bots::Guid BotAdapter::GetVictim(Bots::Guid unit) const
{
    Unit const* found = Find(unit);
    return found ? GuidOf(found->GetVictim()) : Bots::NoGuid;
}

std::vector<Bots::Guid> BotAdapter::GetAttackers(Bots::Guid unit) const
{
    std::vector<Bots::Guid> attackers;
    if (Unit const* found = Find(unit))
        for (Unit const* attacker : found->GetAttackers())
            if (attacker && attacker->IsInWorld() && attacker->IsInMap(me))
                attackers.push_back(GuidOf(attacker));
    return attackers;
}

bool BotAdapter::HasAura(Bots::Guid unit, Bots::SpellId firstRank) const
{
    Unit const* found = Find(unit);
    if (!found)
        return false;
    for (auto const& pair : found->GetSpellAuraHolderMap())
        if (sSpellMgr.GetFirstSpellInChain(pair.first) == firstRank)
            return true;
    return false;
}

bool BotAdapter::HasLootWork(bool mayMove) const
{
    return m_loot && m_loot->HasWork(me, mayMove);
}

Bots::Guid BotAdapter::GetFollowTarget() const
{
    MotionMaster const* motion = me->GetMotionMaster();
    if (motion->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
        return Bots::NoGuid;
    auto const* follow = dynamic_cast<FollowMovementGenerator<Player> const*>(motion->GetCurrent());
    return follow ? GuidOf(follow->GetTarget()) : Bots::NoGuid;
}

Bots::ChaseState BotAdapter::GetChase() const
{
    MotionMaster const* motion = me->GetMotionMaster();
    if (motion->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
        return {Bots::NoGuid, false};
    auto const* chase = dynamic_cast<ChaseMovementGenerator<Player> const*>(motion->GetCurrent());
    if (!chase)
        return {Bots::NoGuid, false};
    return {GuidOf(chase->GetTarget()), chase->GetAngle() != 0.0f};
}

uint32 BotAdapter::GetComboPoints() const
{
    return me->GetComboPoints();
}

Bots::Guid BotAdapter::GetPet() const
{
    return GuidOf(me->GetPet());
}

bool BotAdapter::KnowsSpell(Bots::SpellId spell) const
{
    return me->HasSpell(spell);
}

Bots::SpellId BotAdapter::GetNextRank(Bots::SpellId spell) const
{
    // The chain map can branch; the next rank keeps the first rank.
    uint32 const first = sSpellMgr.GetFirstSpellInChain(spell);
    SpellChainMapNext const& next = sSpellMgr.GetSpellChainNext();
    for (auto it = next.lower_bound(spell); it != next.upper_bound(spell); ++it)
        if (sSpellMgr.GetFirstSpellInChain(it->second) == first)
            return it->second;
    return Bots::NoSpell;
}

bool BotAdapter::CanCast(Bots::SpellId spell, Bots::Guid target) const
{
    // Like CombatBotBaseAI::CanTryToCastSpell of the party bots.
    SpellEntry const* entry = sSpellMgr.GetSpellEntry(spell);
    Unit* unit = Find(target);
    if (!entry || !unit || !unit->IsAlive() || !me->HasSpell(spell))
        return false;
    if (me->IsNonMeleeSpellCasted(false, false, true))
        return false;
    // A spell with a cast time waits until the bot stands. A chase keeps
    // its unit state while the bot stands, so the spline counts.
    if (entry->GetCastTime(me) > 0 && (me->IsMoving() || !me->movespline->Finalized()))
        return false;
    if (!me->IsSpellReady(entry) || me->HasGCD(entry))
        return false;
    // For example a melee weapon for Sinister Strike.
    if (!me->HasItemFitToSpellReqirements(entry))
        return false;
    if (entry->TargetAuraState && !unit->HasAuraState(AuraState(entry->TargetAuraState)))
        return false;
    if (entry->CasterAuraState && !me->HasAuraState(AuraState(entry->CasterAuraState)))
        return false;

    uint32 const cost = Spell::CalculatePowerCost(entry, me);
    Powers const powerType = Powers(entry->powerType);
    if (powerType == POWER_HEALTH ? me->GetHealth() <= cost : me->GetPower(powerType) < cost)
        return false;
    if (entry->GetErrorAtShapeshiftedCast(me->GetShapeshiftForm()) != SPELL_CAST_OK)
        return false;
    if (unit->IsImmuneToSpell(entry, false))
        return false;
    if (entry->IsSpellAppliesAura() && unit->HasAura(entry->Id))
        return false;

    if (unit != me && entry->EffectImplicitTargetA[0] != TARGET_UNIT_CASTER)
    {
        SpellRangeEntry const* range = sSpellRangeStore.LookupEntry(entry->rangeIndex);
        float const distance = me->GetCombatDistance(unit);
        if (range && (distance > range->maxRange || (range->minRange && distance < range->minRange)))
            return false;
        if (!me->IsWithinLOSInMap(unit))
            return false;
    }
    return true;
}

void BotAdapter::CastSpell(Bots::SpellId spell, Bots::Guid target)
{
    SpellEntry const* entry = sSpellMgr.GetSpellEntry(spell);
    Unit* unit = Find(target);
    if (!entry || !unit)
        return;
    if (unit != me)
    {
        me->SetFacingToObject(unit);
        me->SetTargetGuid(unit->GetObjectGuid());
    }
    SpellCastResult const result = me->CastSpell(unit, entry, false);
    if (result != SPELL_CAST_OK)
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s cannot cast %u: result %u", me->GetName(), spell, uint32(result));
}

void BotAdapter::MoveTo(Bots::Position const& destination)
{
    me->GetMotionMaster()->MovePoint(0, destination.x, destination.y, destination.z, MOVE_PATHFINDING | MOVE_RUN_MODE);
}

void BotAdapter::Attack(Bots::Guid target)
{
    if (Unit* unit = Find(target))
    {
        me->SetTargetGuid(unit->GetObjectGuid());
        me->Attack(unit, true);
    }
}

void BotAdapter::Chase(Bots::Guid target)
{
    if (Unit* unit = Find(target))
        me->GetMotionMaster()->MoveChase(unit);
}

void BotAdapter::ChaseBehind(Bots::Guid target)
{
    if (Unit* unit = Find(target))
        me->GetMotionMaster()->MoveChase(unit, BehindYards, M_PI_F);
}

void BotAdapter::ChaseInRange(Bots::Guid target, float yards)
{
    // A chase ends when the victim of the bot is another unit, so the bot
    // takes the unit as its victim, without melee swings.
    if (Unit* unit = Find(target))
    {
        me->SetTargetGuid(unit->GetObjectGuid());
        me->Attack(unit, false);
        me->GetMotionMaster()->MoveChase(unit, yards);
    }
}

void BotAdapter::PetAttack(Bots::Guid target)
{
    // Like the attack command of the pet bar, and like the party bots.
    Pet* pet = me->GetPet();
    Unit* unit = Find(target);
    if (!pet || !pet->IsAlive() || !unit || !pet->AI())
        return;
    pet->GetCharmInfo()->SetIsCommandAttack(true);
    pet->AI()->AttackStart(unit);
    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s sends its pet %s (0x%016llx) to %s: pet victim %s",
        me->GetName(), pet->GetName(), (unsigned long long)pet->GetObjectGuid().GetRawValue(),
        unit->GetObjectGuid().GetString().c_str(), pet->GetVictim() ? pet->GetVictim()->GetObjectGuid().GetString().c_str() : "none");
}

void BotAdapter::Follow(Bots::Guid unit)
{
    if (Unit* found = Find(unit))
    {
        MotionMaster* motion = me->GetMotionMaster();
        motion->Clear();
        motion->MoveFollow(found, frand(MinFollowYards, MaxFollowYards), frand(0.0f, 2 * M_PI_F));
    }
}

void BotAdapter::Loot(bool mayMove)
{
    if (m_loot)
        m_loot->Think(me, mayMove);
}
