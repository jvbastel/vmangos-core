/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "BotAdapter.h"
#include "BotLoot.h"
#include "Group.h"
#include "Log.h"
#include "MotionMaster.h"
#include "spline/MoveSpline.h"
#include "CreatureAI.h"
#include "Pet.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
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

Bots::CastInfo BotAdapter::GetCast(Bots::Guid unit) const
{
    Unit const* found = Find(unit);
    if (!found)
        return {Bots::NoSpell, Bots::CastKind::None, false};
    for (CurrentSpellTypes type : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL})
    {
        Spell const* spell = found->GetCurrentSpell(type);
        if (!spell)
            continue;
        SpellEntry const* info = spell->m_spellInfo;
        Bots::CastKind const kind = info->IsHealSpell() ? Bots::CastKind::Heal
            : info->IsPositiveSpell() ? Bots::CastKind::Other : Bots::CastKind::Harm;
        // The test of Spell::EffectInterruptCast, the effect of Kick and
        // Shield Bash.
        bool const casting = spell->getState() == SPELL_STATE_CASTING
            || (spell->getState() == SPELL_STATE_PREPARING && spell->GetCastTime() > 0);
        bool const interruptible = casting && info->PreventionType == SPELL_PREVENTION_TYPE_SILENCE
            && (type == CURRENT_GENERIC_SPELL ? spell->GetCastTime() > 0 && info->HasSpellInterruptFlag(SPELL_INTERRUPT_FLAG_DAMAGE_PUSHBACK)
                : info->HasChannelInterruptFlag(AURA_INTERRUPT_ACTION_CANCELS));
        return {info->Id, kind, interruptible};
    }
    return {Bots::NoSpell, Bots::CastKind::None, false};
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

float BotAdapter::GetDistanceBetween(Bots::Guid a, Bots::Guid b) const
{
    Unit const* first = Find(a);
    Unit const* second = Find(b);
    return first && second ? first->GetDistance(second) : std::numeric_limits<float>::max();
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

bool BotAdapter::HasDebuff(Bots::Guid unit, Bots::DispelKind kind) const
{
    Unit const* found = Find(unit);
    if (!found)
        return false;
    DispelType type = DISPEL_MAGIC;
    switch (kind)
    {
        case Bots::DispelKind::Magic: type = DISPEL_MAGIC; break;
        case Bots::DispelKind::Curse: type = DISPEL_CURSE; break;
        case Bots::DispelKind::Disease: type = DISPEL_DISEASE; break;
        case Bots::DispelKind::Poison: type = DISPEL_POISON; break;
    }
    for (auto const& pair : found->GetSpellAuraHolderMap())
    {
        SpellAuraHolder const* holder = pair.second;
        if (holder && !holder->IsPositive() && holder->GetSpellProto()->Dispel == uint32(type))
            return true;
    }
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

std::vector<Bots::Guid> BotAdapter::GetGroupMembers() const
{
    std::vector<Bots::Guid> members{GetSelf()};
    if (Group* group = me->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player const* member = ref->getSource();
            if (member && member != me && member->IsInWorld() && member->IsInMap(me))
                members.push_back(GuidOf(member));
        }
    return members;
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
    // Like CombatBotBaseAI::DoCastSpell: a spell with a cast time ends when the
    // caster moves (for example when it follows or chases a unit), so stop the bot.
    if (result == SPELL_CAST_OK && entry->GetCastTime(me) > 0 && (me->IsMoving() || !me->IsStopped()))
        me->StopMoving();
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
    {
        // A melee chase, also after a ranged one (ChaseToRange).
        me->SetCasterChaseDistance(0.0f);
        me->GetMotionMaster()->MoveChase(unit);
    }
}

void BotAdapter::ChaseBehind(Bots::Guid target)
{
    Unit* unit = Find(target);
    if (!unit)
        return;
    me->SetCasterChaseDistance(0.0f);
    // A chase with an offset does not move a bot that stands close to the
    // unit already, so a bot in front of the unit first walks behind it. The
    // next think then starts the chase from behind.
    if (unit->HasInArc(me, M_PI_F))
    {
        float x, y, z;
        unit->GetNearPoint(me, x, y, z, me->GetObjectBoundingRadius(), BehindYards, unit->GetOrientation() + M_PI_F);
        me->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING | MOVE_RUN_MODE);
        return;
    }
    me->GetMotionMaster()->MoveChase(unit, BehindYards, M_PI_F);
}

void BotAdapter::ChaseToRange(Bots::Guid target, float yards)
{
    // Like CombatBotBaseAI::BeginChasing of a ranged party bot: the chase
    // path ends at the caster chase distance, in line of sight. A chase
    // needs the target as the victim, so the bot attacks it without a
    // melee swing.
    Unit* unit = Find(target);
    if (!unit)
        return;
    MotionMaster* motion = me->GetMotionMaster();
    if (me->GetMinChaseDistance() == yards && motion->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE
        && me->GetVictim() == unit)
        return;
    me->SetCasterChaseDistance(yards);
    me->SetTargetGuid(unit->GetObjectGuid());
    me->Attack(unit, false);
    motion->MoveChase(unit, 1.0f);
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
