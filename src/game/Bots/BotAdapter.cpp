/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "BotAdapter.h"
#include "BotLoot.h"
#include "BotMgr.h"
#include "GameRouteNav.h"
#include "BotGeometry.h"
#include "GhostWalk.h"
#include "CastResult.h"
#include "ExtraPackWatch.h"
#include "MemberSpot.h"
#include "Bag.h"
#include "Corpse.h"
#include "Group.h"
#include "GridMap.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "spline/MoveSpline.h"
#include "CellImpl.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Pet.h"
#include "ObjectAccessor.h"
#include "Packet.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "SQLStorages.h"
#include "TargetedMovementGenerator.h"
#include "WorldSession.h"
#include "Utilities/Random.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <list>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // True the first time that bot `bot` is a ghost of `dungeon` with no
    // ghost route, since the start of mangosd: WalkAsGhost logs it once.
    // The bots think on the map threads, so a lock guards the memory.
    bool FirstGhostWithoutRoute(Bots::Guid bot, uint32 dungeon)
    {
        static std::mutex mutex;
        static std::set<std::pair<Bots::Guid, uint32>> logged;
        std::lock_guard<std::mutex> lock(mutex);
        return logged.emplace(bot, dungeon).second;
    }

    // The bot library keeps its own copy of the SpellCastResult names
    // (bots/CastResult.h), for client build 1.12.1. Check the copy against the
    // real enum here. Older client builds have fewer results: they log only
    // the number.
#if SUPPORTED_CLIENT_BUILD == CLIENT_BUILD_1_12_1
#define WOWAI_CAST_RESULT_VALUE(name) name,
    constexpr SpellCastResult CopiedFailedResults[] = { WOWAI_SPELL_FAILED_RESULTS(WOWAI_CAST_RESULT_VALUE) };
#undef WOWAI_CAST_RESULT_VALUE

    constexpr bool CopyIsInEnumOrder()
    {
        for (std::size_t i = 0; i < sizeof(CopiedFailedResults) / sizeof(CopiedFailedResults[0]); ++i)
            if (uint32(CopiedFailedResults[i]) != i)
                return false;
        return true;
    }

    static_assert(CopyIsInEnumOrder(), "bots/CastResult.h lists the SpellCastResult names out of enum order");
    static_assert(sizeof(CopiedFailedResults) / sizeof(CopiedFailedResults[0]) == SPELL_FAILED_UNKNOWN + 1,
        "bots/CastResult.h does not list every SpellCastResult failure");
    static_assert(SPELL_CAST_OK == Bots::SpellCastOk, "bots/CastResult.h has a wrong SPELL_CAST_OK");
#endif

    // "result 62 (SPELL_FAILED_NOT_STANDING)", for the bot log.
    std::string CastResultLogText(SpellCastResult result)
    {
#if SUPPORTED_CLIENT_BUILD == CLIENT_BUILD_1_12_1
        return Bots::CastResultText(uint32(result));
#else
        return "result " + std::to_string(uint32(result));
#endif
    }

    // KeepMovingTo: a bot that walks to a spot this near keeps its path.
    float const SameDestinationYards = 1.0f;

    // The living creatures within `range` of `source`, for GetEnemiesNear.
    struct LivingCreatureCheck
    {
        WorldObject const* source;
        float range;

        WorldObject const& GetFocusObject() const { return *source; }
        bool operator()(Creature* creature) const
        {
            return creature->IsAlive() && source->IsWithinDistInMap(creature, range);
        }
    };

    // A following bot keeps this distance to the unit, at a random angle.
    float const MinFollowYards = 3.0f;
    float const MaxFollowYards = 6.0f;
    // A bot behind its target stays this far from it, like the melee party
    // bots of CombatBotBaseAI.
    float const BehindYards = 1.0f;
    // The height of a body over the ground, for the rays to a wall.
    float const BodyHeightYards = 2.0f;

    Bots::Guid GuidOf(Unit const* unit)
    {
        return unit ? unit->GetObjectGuid().GetRawValue() : Bots::NoGuid;
    }

    struct CurrentSpell
    {
        CurrentSpellTypes type;
        Spell const* spell;
    };

    // The one loop over the current spells of a unit: the generic spell,
    // then the channeled spell, then with `autoRepeat` the auto-repeat spell
    // (Shoot, Auto Shot). The cast of the unit comes first.
    std::vector<CurrentSpell> CurrentSpellsOf(Unit const* unit, bool autoRepeat)
    {
        std::vector<CurrentSpell> spells;
        for (CurrentSpellTypes type : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL})
        {
            if (type == CURRENT_AUTOREPEAT_SPELL && !autoRepeat)
                continue;
            if (Spell const* spell = unit->GetCurrentSpell(type))
                spells.push_back({type, spell});
        }
        return spells;
    }

    // The first ranks of the spells that resurrect a dead player:
    // Resurrection, Redemption, and Ancestral Spirit.
    uint32 const ResurrectSpells[] = {2006, 7328, 2008};

    // The food and the drink of a level: the item of the highest level
    // that the bot may use. Vendor food and water of the Horde.
    struct RestItem
    {
        uint32 level;
        uint32 food;
        uint32 drink;
    };
    RestItem const RestItems[] = {
        {45, 8950, 8766}, // Homemade Cherry Pie, Morning Glory Dew
        {35, 4601, 1645}, // Soft Banana Bread, Moonberry Juice
        {25, 4544, 1708}, // Mulgore Spice Bread, Sweet Nectar
        {15, 4542, 1205}, // Moist Cornbread, Melon Juice
        {5, 4541, 1179},  // Freshly Baked Bread, Ice Cold Milk
        {1, 4540, 159},   // Tough Hunk of Bread, Refreshing Spring Water
    };
    // The bot refills its food and its drink to this many before each use.
    uint32 const RestItemStack = 5;

    RestItem const& RestItemFor(uint32 level)
    {
        for (RestItem const& item : RestItems)
            if (level >= item.level)
                return item;
        return RestItems[sizeof(RestItems) / sizeof(RestItems[0]) - 1];
    }

    Item* FindBagItem(Player const* bot, uint32 entry)
    {
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                if (item->GetEntry() == entry)
                    return item;
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = dynamic_cast<Bag*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot)))
                for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                    if (Item* item = bag->GetItemByPos(uint8(slot)))
                        if (item->GetEntry() == entry)
                            return item;
        return nullptr;
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
    return GetCast(unit).spell;
}

Bots::CastInfo BotAdapter::GetCast(Bots::Guid unit) const
{
    return CastOf(Find(unit));
}

Bots::CastKind BotAdapter::CastKindOf(SpellEntry const* spell)
{
    if (!spell)
        return Bots::CastKind::Other;
    return spell->IsHealSpell() ? Bots::CastKind::Heal
        : spell->IsPositiveSpell() ? Bots::CastKind::Other : Bots::CastKind::Harm;
}

Bots::CastInfo BotAdapter::CastOf(Unit const* unit)
{
    std::vector<CurrentSpell> const spells = unit ? CurrentSpellsOf(unit, false) : std::vector<CurrentSpell>();
    if (spells.empty())
        return {Bots::NoSpell, Bots::CastKind::None, false};
    CurrentSpellTypes const type = spells.front().type;
    Spell const* spell = spells.front().spell;
    SpellEntry const* info = spell->m_spellInfo;
    Bots::CastKind const kind = CastKindOf(info);
    // The test of Spell::EffectInterruptCast, the effect of Kick and
    // Shield Bash.
    bool const casting = spell->getState() == SPELL_STATE_CASTING
        || (spell->getState() == SPELL_STATE_PREPARING && spell->GetCastTime() > 0);
    bool const interruptible = casting && info->PreventionType == SPELL_PREVENTION_TYPE_SILENCE
        && (type == CURRENT_GENERIC_SPELL ? spell->GetCastTime() > 0 && info->HasSpellInterruptFlag(SPELL_INTERRUPT_FLAG_DAMAGE_PUSHBACK)
            : info->HasChannelInterruptFlag(AURA_INTERRUPT_ACTION_CANCELS));
    return {info->Id, kind, interruptible};
}

Bots::MemberAttacks BotAdapter::AttacksOf(Player const* member, bool bot)
{
    Bots::MemberAttacks attacks;
    attacks.bot = bot;
    attacks.victim = GuidOf(member->GetVictim());
    for (CurrentSpell const& current : CurrentSpellsOf(member, true))
    {
        ObjectGuid const target = current.spell->m_targets.getUnitTargetGuid();
        if (!target.IsEmpty())
            attacks.spellTargets.push_back(target.GetRawValue());
    }
    if (Pet const* pet = member->GetPet())
        if (pet->IsAlive())
            attacks.petVictim = GuidOf(pet->GetVictim());
    attacks.selection = member->GetTargetGuid().GetRawValue();
    return attacks;
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

float BotAdapter::GetSpellDistance(Bots::Guid unit) const
{
    // The distance of Spell::CheckRange and of the range check of CanCast.
    Unit const* found = Find(unit);
    return found ? me->GetCombatDistance(found) : std::numeric_limits<float>::max();
}

float BotAdapter::GetSpellDistanceFrom(Bots::Position const& place, Bots::Guid unit) const
{
    // GetCombatDistance with the bot on the ground at the place.
    Unit const* found = Find(unit);
    if (!found)
        return std::numeric_limits<float>::max();
    float const ground = GetGround(place).z;
    float const centers = found->GetDistance3dToCenter(place.x, place.y, ground);
    return std::max(0.0f, centers - me->GetCombatReach() - found->GetCombatReach());
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

Unit* BotAdapter::ChaseTargetOf(Player const* player)
{
    MotionMaster const* motion = player->GetMotionMaster();
    if (motion->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
        return nullptr;
    auto const* chase = dynamic_cast<ChaseMovementGenerator<Player> const*>(motion->GetCurrent());
    return chase ? chase->GetTarget() : nullptr;
}

uint32 BotAdapter::GetComboPoints() const
{
    // The points sit on one unit. After a switch to another target (an
    // enemy healer, or `attack`), the bot has none on its new target, and
    // Slice and Dice would fail with no combo points on each think.
    Unit const* victim = me->GetVictim();
    return victim && victim->GetObjectGuid() == me->GetComboTargetGuid() ? me->GetComboPoints() : 0;
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
    if (!entry || !unit || !me->HasSpell(spell))
        return false;
    // A resurrect targets a dead player that has not released, with no
    // resurrect waiting for its answer. Any other spell targets the living.
    if (entry->HasEffect(SPELL_EFFECT_RESURRECT) || entry->HasEffect(SPELL_EFFECT_RESURRECT_NEW))
    {
        Player const* player = unit->IsPlayer() ? static_cast<Player const*>(unit) : nullptr;
        if (!player || player->IsAlive() || player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) || player->IsRessurectRequested())
            return false;
    }
    else if (!unit->IsAlive())
        return false;
    if (me->IsNonMeleeSpellCasted(false, false, true))
        return false;
    // A bot that eats or drinks sits: it casts again when it stands.
    if (me->GetStandState() != UNIT_STAND_STATE_STAND)
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

    // A melee ability (Kick, Shield Bash, Sinister Strike): like
    // Spell::CheckRange. Without it, a Kick on a caster that is not the
    // victim failed on each think (out of range, or not in front).
    if (unit != me && entry->rangeIndex == SPELL_RANGE_IDX_COMBAT && !entry->IsNextMeleeSwingSpell())
    {
        if (!me->CanReachWithMeleeSpellAttack(unit))
            return false;
        // CastSpell turns the bot to its victim. Another unit must be in
        // front already.
        if (unit != me->GetVictim() && !me->IsFacingTarget(unit))
            return false;
    }
    else if (unit != me && entry->EffectImplicitTargetA[0] != TARGET_UNIT_CASTER)
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

bool BotAdapter::LevelAllowsRank(Bots::SpellId rank, Bots::Guid target) const
{
    // Like the SPELL_FAILED_LOWLEVEL check of Spell::CheckTarget. A player
    // gets a lower rank from WorldSession::HandleCastSpellOpcode, a bot not.
    SpellEntry const* entry = sSpellMgr.GetSpellEntry(rank);
    Unit const* unit = Find(target);
    if (!entry || !unit || unit == me || entry->HasAttribute(SPELL_ATTR_EX2_ALLOW_LOW_LEVEL_BUFF))
        return true;
    return sSpellMgr.SelectAuraRankForLevel(entry, unit->GetLevel()) == entry;
}

bool BotAdapter::IsCastLocked(Bots::SpellId spell) const
{
    // Like the states of Spell::CheckCasterAuras, without the spells that
    // grant an immunity to them (the bots cast none).
    SpellEntry const* entry = sSpellMgr.GetSpellEntry(spell);
    if (!entry || entry->IsIgnoringCasterAndTargetRestrictions())
        return false;
    if (me->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_STUNNED | UNIT_FLAG_CONFUSED | UNIT_FLAG_FLEEING))
        return true;
    if (entry->PreventionType == SPELL_PREVENTION_TYPE_SILENCE)
        return me->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SILENCED) || me->CheckLockout(entry->GetSpellSchoolMask());
    if (entry->PreventionType == SPELL_PREVENTION_TYPE_PACIFY)
        return me->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PACIFIED);
    return false;
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
    // The cast that an interrupt stops, read before the interrupt lands.
    uint32 const castToStop = entry->HasEffect(SPELL_EFFECT_INTERRUPT_CAST) ? CastOf(unit).spell : 0;
    SpellCastResult const result = me->CastSpell(unit, entry, false);
    // An interrupt attempt of a member of a scripted run: the run counts
    // the double interrupts (spec section 10, Results).
    if (result == SPELL_CAST_OK && castToStop && sBotMgr.IsScriptedRunGroup(me->GetGroup()))
        sBotMgr.Run().QueueCombatNote({RunCombatNote::Kind::InterruptAttempt, me->GetObjectGuid(), unit->GetObjectGuid(),
            castToStop, 0});
    // Like CombatBotBaseAI::DoCastSpell: a spell with a cast time ends when the
    // caster moves (for example when it follows or chases a unit), so stop the bot.
    if (result == SPELL_CAST_OK && entry->GetCastTime(me) > 0 && (me->IsMoving() || !me->IsStopped()))
        me->StopMoving();
    if (result != SPELL_CAST_OK)
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s cannot cast %u: %s", me->GetName(), spell,
            CastResultLogText(result).c_str());
}

void BotAdapter::MoveTo(Bots::Position const& destination)
{
    // The library keeps the height of the bot: put the place on the ground.
    Bots::Position const ground = GetGround(destination);
    me->GetMotionMaster()->MovePoint(0, ground.x, ground.y, ground.z, MOVE_PATHFINDING | MOVE_RUN_MODE);
}

void BotAdapter::KeepMovingTo(Bots::Position const& destination)
{
    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE && !me->movespline->Finalized())
    {
        Vector3 const end = me->movespline->FinalDestination();
        if (Bots::Distance2d({end.x, end.y, end.z}, destination) < SameDestinationYards)
            return;
    }
    MoveTo(destination);
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

uint32 BotAdapter::GetEntry(Bots::Guid unit) const
{
    Unit const* found = Find(unit);
    return found && found->IsCreature() ? found->GetEntry() : 0;
}

Bots::Position BotAdapter::GetPosition(Bots::Guid unit) const
{
    Unit const* found = Find(unit);
    if (!found)
        return {0.0f, 0.0f, 0.0f};
    return {found->GetPositionX(), found->GetPositionY(), found->GetPositionZ()};
}

float BotAdapter::GetWallDistance(Bots::Position const& from, float angle, float maxYards) const
{
    // A ray of the collision models (vmaps and game objects) at the height
    // of a body. A rising floor counts as a wall too.
    float const z = from.z + BodyHeightYards;
    float x = from.x + maxYards * std::cos(angle);
    float y = from.y + maxYards * std::sin(angle);
    float hitZ = z;
    if (!me->GetMap()->GetLosHitPosition(from.x, from.y, z, x, y, hitZ, 0.0f))
        return maxYards;
    return std::min(maxYards, Bots::Distance2d(from, {x, y, hitZ}));
}

bool BotAdapter::IsHarmfulGround(Bots::Position const& place) const
{
    // The ground under the place (as MoveTo puts it), and the liquid from a
    // bit above the higher of the two heights: a ground deep under the lava
    // is not found from the ground itself.
    float const ground = GetGround(place).z;
    float const top = std::max(place.z, ground) + BodyHeightYards;
    GridMapLiquidData liquid;
    GridMapLiquidStatus const status = me->GetTerrain()->getLiquidStatus(place.x, place.y, top,
        MAP_LIQUID_TYPE_MAGMA | MAP_LIQUID_TYPE_SLIME, &liquid);
    return status != LIQUID_MAP_NO_WATER && liquid.level > ground;
}

bool BotAdapter::CanSeeFrom(Bots::Position const& place, Bots::Guid unit) const
{
    // Like the line of sight check of CanCast, from the ground at the place.
    Unit const* target = Find(unit);
    if (!target)
        return false;
    float const ground = GetGround(place).z;
    float x, y, z;
    target->GetLosCheckPosition(x, y, z);
    return me->IsWithinLOSAtPosition(place.x, place.y, ground, x, y, z, true, 0.0f);
}

Unit const* BotAdapter::FindAnywhere(Bots::Guid unit) const
{
    if (Unit const* found = Find(unit))
        return found;
    ObjectGuid const guid(unit);
    if (!guid.IsPlayer())
        return nullptr;
    Player const* player = ObjectAccessor::FindPlayer(guid);
    return player && player->IsInWorld() ? player : nullptr;
}

Bots::LifeState BotAdapter::GetLifeState(Bots::Guid unit) const
{
    Unit const* found = FindAnywhere(unit);
    if (!found || found->IsAlive())
        return Bots::LifeState::Alive;
    if (found->IsPlayer() && static_cast<Player const*>(found)->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
        return Bots::LifeState::Ghost;
    return Bots::LifeState::Dead;
}

Bots::Guid BotAdapter::GetGroupLeader() const
{
    Group const* group = me->GetGroup();
    return group ? group->GetLeaderGuid().GetRawValue() : Bots::NoGuid;
}

bool BotAdapter::CanResurrect(Bots::Guid unit) const
{
    Unit const* found = Find(unit);
    if (!found || !found->IsAlive() || !found->IsPlayer())
        return false;
    Player const* player = static_cast<Player const*>(found);
    for (auto const& pair : player->GetSpellMap())
    {
        if (pair.second.state == PLAYERSPELL_REMOVED || pair.second.disabled)
            continue;
        uint32 const firstRank = sSpellMgr.GetFirstSpellInChain(pair.first);
        for (uint32 resurrect : ResurrectSpells)
            if (firstRank == resurrect)
                return true;
    }
    return false;
}

Bots::Power BotAdapter::GetMana(Bots::Guid unit) const
{
    Unit const* found = Find(unit);
    if (!found || found->GetPowerType() != POWER_MANA)
        return {0, 0};
    return {found->GetPower(POWER_MANA), found->GetMaxPower(POWER_MANA)};
}

bool BotAdapter::IsEating() const
{
    return me->HasAuraType(SPELL_AURA_MOD_REGEN);
}

bool BotAdapter::IsDrinking() const
{
    return me->HasAuraType(SPELL_AURA_MOD_POWER_REGEN);
}

Bots::Position BotAdapter::GetGround(Bots::Position const& place) const
{
    Bots::Position ground = place;
    me->UpdateAllowedPositionZ(ground.x, ground.y, ground.z);
    return ground;
}

bool BotAdapter::FindGroundPath(Bots::Position const& from, Bots::Position const& to,
    std::vector<Bots::Position>& path) const
{
    path.clear();
    GameRouteNav nav(me);
    return nav.Ready() && nav.OnMesh(to) && nav.Path(from, to, path);
}

std::vector<Bots::NearEnemy> BotAdapter::GetEnemiesNear(float yards) const
{
    std::list<Creature*> creatures;
    LivingCreatureCheck check = {me, yards};
    MaNGOS::CreatureListSearcher<LivingCreatureCheck> searcher(creatures, check);
    Cell::VisitGridObjects(me, searcher, yards);
    std::vector<Bots::NearEnemy> enemies;
    for (Creature const* creature : creatures)
        if (!creature->IsPet() && creature->IsHostileTo(me))
            enemies.push_back({{creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ()},
                creature->GetAttackDistance(me), creature->GetObjectGuid().GetRawValue()});
    return enemies;
}

void BotAdapter::Release()
{
    // Like the release button of a client.
    me->GetSession()->QueuePacket(std::make_unique<NullClientPacket>(CMSG_REPOP_REQUEST));
}

void BotAdapter::WalkAsGhost()
{
    if (me->IsAlive() || !me->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) || me->IsBeingTeleported())
        return;
    // A scripted run revives its ghosts itself (BotRun::ReviveTheDead).
    if (sBotMgr.IsScriptedRunGroup(me->GetGroup()))
        return;
    Corpse const* corpse = me->GetCorpse();
    if (!corpse)
        return;
    uint32 const dungeon = corpse->GetMapId();
    Bots::Route const* route = sBotMgr.GhostRoute(dungeon);
    if (!route)
    {
        if (FirstGhostWithoutRoute(GetSelf(), dungeon))
        {
            MapEntry const* entry = sMapStorage.LookupEntry<MapEntry>(dungeon);
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %s is a ghost of map %u (%s), which has no ghost route: it stays at the graveyard",
                me->GetName(), dungeon, entry && entry->name ? entry->name : "?");
        }
        return;
    }
    if (route->mapId != me->GetMapId())
        return;

    Bots::GhostStep const step = Bots::NextGhostStep(*route, GetPosition(GetSelf()));
    if (!step.atPortal)
    {
        KeepMovingTo(step.next);
        return;
    }
    // Into the portal, like the area trigger of a client: a ghost whose
    // corpse is in the dungeon is alive again at its entrance. Database:
    // TeleportTo resurrects the ghost in memory (Player::TeleportTo), and the
    // normal player save writes it. Client: a bot has no client; BotAI answers
    // the SMSG_NEW_WORLD of a far teleport with the worldport ack itself.
    AreaTriggerTeleport const* entrance = sObjectMgr.GetMapEntranceTrigger(dungeon);
    if (!entrance)
        return;
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] %s enters map %u as a ghost", me->GetName(), dungeon);
    me->TeleportTo(entrance->destination.mapId, entrance->destination.x, entrance->destination.y,
        entrance->destination.z, entrance->destination.o);
}

void BotAdapter::UseRestItem(uint32 entry)
{
    uint32 const count = me->GetItemCount(entry);
    if (count < RestItemStack)
    {
        // Silently: no loot message and no chat line.
        ItemPosCountVec dest;
        if (me->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry, RestItemStack - count) == EQUIP_ERR_OK
            && me->StoreNewItem(dest, entry, true))
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s refills item %u silently to %u", me->GetName(), entry, RestItemStack);
    }
    Item* item = FindBagItem(me, entry);
    if (!item)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s has no room for item %u", me->GetName(), entry);
        return;
    }

    me->StopMoving();
    me->GetMotionMaster()->Clear(false, true);
    me->GetMotionMaster()->MoveIdle();
    SpellCastTargets targets;
    targets.setUnitTarget(me);
    me->CastItemUseSpell(item, targets);
}

void BotAdapter::Eat()
{
    UseRestItem(RestItemFor(me->GetLevel()).food);
}

void BotAdapter::Drink()
{
    UseRestItem(RestItemFor(me->GetLevel()).drink);
}

void BotAdapter::WarnExtraPacks(uint32 packs)
{
    // A party line like the refusals of BotAI: BotMgr::SendLines says it on
    // the world thread, and a scripted run notes the warn there.
    BotLine line{me->GetObjectGuid(), CHAT_MSG_PARTY, ObjectGuid(), Bots::ExtraPackWarnLine(packs)};
    line.extraPackWarn = true;
    sBotMgr.QueueLine(std::move(line));
}

void BotAdapter::SayGroupWait(uint32 members)
{
    sBotMgr.QueueLine({me->GetObjectGuid(), CHAT_MSG_PARTY, ObjectGuid(), Bots::GroupWaitLine(members)});
}

Bots::RangedWeapon BotAdapter::GetRangedWeapon() const
{
    Bots::RangedWeapon weapon;
    Item const* item = me->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    ItemPrototype const* proto = item ? item->GetProto() : nullptr;
    if (!proto || proto->Class != ITEM_CLASS_WEAPON)
        return weapon;
    switch (proto->SubClass)
    {
        case ITEM_SUBCLASS_WEAPON_THROWN: weapon.kind = Bots::RangedKind::Thrown; break;
        case ITEM_SUBCLASS_WEAPON_BOW: weapon.kind = Bots::RangedKind::Bow; break;
        case ITEM_SUBCLASS_WEAPON_GUN: weapon.kind = Bots::RangedKind::Gun; break;
        case ITEM_SUBCLASS_WEAPON_CROSSBOW: weapon.kind = Bots::RangedKind::Crossbow; break;
        default: return weapon; // A wand needs no pull rules.
    }
    weapon.entry = proto->ItemId;
    if (weapon.kind == Bots::RangedKind::Thrown)
    {
        // Each throw uses up one of the worn stack (Spell::TakeAmmo).
        weapon.ammoEntry = proto->ItemId;
        weapon.ammoCount = item->GetCount();
        return weapon;
    }
    // The ammo of the ammo slot, when it fits the weapon: arrows for a bow
    // or a crossbow, bullets for a gun.
    uint32 const ammo = me->GetUInt32Value(PLAYER_AMMO_ID);
    ItemPrototype const* ammoProto = ammo ? sObjectMgr.GetItemPrototype(ammo) : nullptr;
    if (ammoProto && ammoProto->SubClass == proto->AmmoType)
    {
        weapon.ammoEntry = ammo;
        weapon.ammoCount = me->GetItemCount(ammo);
    }
    return weapon;
}

uint32 BotAdapter::GetLevel(Bots::Guid unit) const
{
    Unit const* found = Find(unit);
    return found ? found->GetLevel() : 0;
}

bool BotAdapter::IsMoving(Bots::Guid unit) const
{
    Unit const* found = Find(unit);
    return found && (found->IsMoving() || !found->movespline->Finalized());
}

bool BotAdapter::HasRoomFor(Bots::AmmoTopUp const& topUp) const
{
    if (!topUp.entry)
        return false;
    Item const* ranged = me->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    if (ranged && ranged->GetEntry() == topUp.entry)
        return ranged->GetCount() < std::min(topUp.count, ranged->GetMaxStackCount());
    uint32 const have = me->GetItemCount(topUp.entry);
    if (have >= topUp.count)
        return me->GetUInt32Value(PLAYER_AMMO_ID) != topUp.entry;
    ItemPosCountVec dest;
    return me->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, topUp.entry, topUp.count - have) == EQUIP_ERR_OK;
}

void BotAdapter::TopUpAmmo(Bots::AmmoTopUp const& topUp)
{
    if (!topUp.entry || !topUp.count)
        return;
    // A thrown stack grows in place, silently.
    Item* ranged = me->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    if (ranged && ranged->GetEntry() == topUp.entry)
    {
        uint32 const count = std::min(topUp.count, ranged->GetMaxStackCount());
        if (ranged->GetCount() < count)
        {
            ranged->SetCount(count);
            ranged->SetState(ITEM_CHANGED, me);
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s tops up its thrown weapon %u silently to %u", me->GetName(),
                topUp.entry, count);
        }
        return;
    }
    // Arrows or bullets: into the bags, silently, then into the ammo slot.
    uint32 const have = me->GetItemCount(topUp.entry);
    if (have < topUp.count)
    {
        ItemPosCountVec dest;
        if (me->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, topUp.entry, topUp.count - have) == EQUIP_ERR_OK
            && me->StoreNewItem(dest, topUp.entry, true))
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Bots] %s tops up ammo %u silently to %u", me->GetName(), topUp.entry,
                topUp.count);
    }
    me->SetAmmo(topUp.entry);
}

void BotAdapter::Stand()
{
    me->StopMoving();
    me->GetMotionMaster()->Clear(false, true);
    me->GetMotionMaster()->MoveIdle();
}

bool BotAdapter::IsMeleeAttacking() const
{
    return me->GetVictim() && me->HasUnitState(UNIT_STATE_MELEE_ATTACKING);
}
