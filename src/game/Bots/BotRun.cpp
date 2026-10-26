/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "BotRun.h"
#include "Bag.h"
#include "BotAdapter.h"
#include "BotHooks.h"
#include "BotMgr.h"
#include "BotRecipe.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Corpse.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "RouteFiles.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "WipeRevive.h"
#include "Utilities/Random.h"
#include "WorldSession.h"

#include <chrono>
#include <cmath>
#include <fstream>
#include <list>
#include <sstream>

namespace
{
    // The fixed gear template of the test driver.
    char const* const DriverGear = "test_driver_mage";
    // The longest time of one reset phase: the logins, the logouts, and the
    // teleport out of the dungeon.
    int64 const ResetPhaseLimitMs = 60000;
    // The test driver finds the creatures of `target` and `expect` within
    // this distance.
    float const SearchYards = 300.0f;
    // The test driver arrived when it is this much past the radius of the
    // place of its `move`.
    float const ArriveSlackYards = 1.5f;

    int64 SteadyMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    bool ReadFile(std::string const& path, std::string& text)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::ostringstream out;
        out << in.rdbuf();
        text = out.str();
        return true;
    }

    Player* FindByName(std::string const& name)
    {
        std::string wanted = name;
        if (!normalizePlayerName(wanted))
            return nullptr;
        return ObjectAccessor::FindPlayerByName(wanted.c_str());
    }

    bool InWorldAndStill(Player const* player)
    {
        return player && player->IsInWorld() && !player->IsBeingTeleported();
    }

    // A member that lives and stands in the world, for the revive of the
    // dead and the threat watch. A member in a teleport counts as neither
    // living nor dead.
    bool IsLivingMember(Player const* player)
    {
        return InWorldAndStill(player) && player->IsAlive();
    }

    struct LivingNearCheck
    {
        WorldObject const* source;
        float range;

        WorldObject const& GetFocusObject() const { return *source; }
        bool operator()(Creature* creature) const
        {
            return creature->IsAlive() && source->IsWithinDistInMap(creature, range);
        }
    };

    // The creature of a spawn of the route on the map of `player`, or null.
    Creature* SpawnOf(Player const* player, Bots::RouteMob const& mob)
    {
        return player->GetMap()->GetCreature(ObjectGuid(HIGHGUID_UNIT, mob.entry, mob.spawnGuid));
    }

    void FullHealthAndPower(Unit* unit)
    {
        unit->SetHealth(unit->GetMaxHealth());
        if (unit->GetPowerType() == POWER_RAGE)
            unit->SetPower(POWER_RAGE, 0);
        else
            unit->SetPower(unit->GetPowerType(), unit->GetMaxPower(unit->GetPowerType()));
    }

    // The auras that a spell, an item, or a fight left on the member: all
    // but the passive ones of its talents and spells.
    void RemoveCastAuras(Unit* unit)
    {
        std::vector<uint32> spells;
        for (auto const& pair : unit->GetSpellAuraHolderMap())
            if (!pair.second->IsPassive())
                spells.push_back(pair.first);
        for (uint32 spell : spells)
            unit->RemoveAurasDueToSpell(spell);
    }

    bool IsFoodOrDrink(Item const* item)
    {
        // The spell categories of food (11) and drink (59).
        ItemPrototype const* proto = item->GetProto();
        if (!proto || proto->Class != ITEM_CLASS_CONSUMABLE)
            return false;
        for (auto const& spell : proto->Spells)
            if (spell.SpellCategory == 11 || spell.SpellCategory == 59)
                return true;
        return false;
    }

    // Empties the bags except the food and the drink. Returns the count of
    // the destroyed stacks.
    uint32 EmptyBags(Player* player)
    {
        uint32 destroyed = 0;
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                if (!IsFoodOrDrink(item))
                {
                    player->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
                    ++destroyed;
                }
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = dynamic_cast<Bag*>(player->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot)))
                for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                    if (Item* item = bag->GetItemByPos(uint8(slot)))
                        if (!IsFoodOrDrink(item))
                        {
                            player->DestroyItem(bagSlot, uint8(slot), true);
                            ++destroyed;
                        }
        return destroyed;
    }

    char const* PhaseName(int phase)
    {
        static char const* const names[] = {"idle", "reset: login", "reset: out of the dungeon", "reset: logout",
            "reset: login again", "script",
            "closing: the group waits for the chat of the run", "passed", "failed"};
        return names[phase];
    }
}

bool BotRun::Start(std::string const& dataDir, std::string const& scriptName, std::string const& scenario, std::string& error)
{
    // A new run does not wait for the chat of the last run.
    EndClosing("a new run starts");
    if (m_phase != Phase::Idle && !IsOver())
    {
        error = "A run is running: " + Status();
        return false;
    }
    std::string text;
    std::string const scriptPath = dataDir + "/bottests/" + scriptName + ".txt";
    if (!Bots::IsValidRouteName(scriptName) || !ReadFile(scriptPath, text))
    {
        error = "Cannot read the script file " + scriptPath + ".";
        return false;
    }
    Bots::RunScriptResult const script = Bots::ParseRunScript(text);
    if (!script.ok)
    {
        error = scriptPath + ": " + script.error;
        return false;
    }
    if (!Bots::FindScenario(script.script, scenario))
    {
        error = "The script " + scriptName + " has no scenario " + scenario + ".";
        return false;
    }
    std::string const routePath = RouteFiles::PathOf(script.script.route);
    if (!Bots::IsValidRouteName(script.script.route) || !ReadFile(routePath, text))
    {
        error = "Cannot read the route file " + routePath + ".";
        return false;
    }
    Bots::RouteResult const route = Bots::ParseRoute(text);
    if (!route.ok)
    {
        error = routePath + ": " + route.error;
        return false;
    }
    std::string const check = Bots::CheckRunScript(script.script, route.route);
    if (!check.empty())
    {
        error = scriptPath + ": " + check;
        return false;
    }
    std::vector<std::string> names = script.script.group;
    names.push_back(script.script.driver);
    for (std::string const& name : names)
        if (sBotMgr.OnlineStateOf(name) == BotOnlineState::Unknown)
        {
            error = "No bot is named " + name + ". The test realm setup makes the bots and the test driver.";
            return false;
        }

    m_script = script.script;
    m_route = route.route;
    m_scenario = scenario;
    m_runner.reset(new Bots::ScriptRunner(Bots::FindScenario(m_script, scenario)->commands));
    m_startMs = SteadyMs();
    m_result.clear();
    m_bossKills.clear();
    m_threat = Bots::ThreatWatch();
    m_threatNames.clear();
    m_lostMobs = Bots::LostMobWatch(sBotMgr.GetSettings().lostMobMs);
    m_lostMobNames.clear();
    m_groupId = 0;
    m_teleportRest = false;
    m_moving = false;
    ++m_number;
    sBotMgr.SetTestDriver(sObjectMgr.GetPlayerGuidByName(m_script.driver));
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s %s starts, test driver %s", m_number, m_script.name.c_str(),
        m_scenario.c_str(), m_script.driver.c_str());
    SetPhase(Phase::LoginFirst);
    return true;
}

bool BotRun::Stop(std::string& error)
{
    if (m_phase == Phase::Idle || IsOver())
    {
        error = "No run is running.";
        return false;
    }
    // The run has its result: a stop only ends the wait for its chat.
    if (EndClosing("stopped by a GM"))
        return true;
    Fail("stopped by a GM");
    return true;
}

std::string BotRun::Status() const
{
    if (m_phase == Phase::Idle)
        return "No run yet.";
    if (IsOver())
        return m_result;
    std::ostringstream line;
    line << "Run " << m_number << " " << m_script.name << " " << m_scenario << ": running, " << PhaseName(int(m_phase));
    if (m_phase == Phase::Script)
        line << ", " << m_runner->Current();
    line << " (" << (SteadyMs() - m_startMs) / 1000 << " s)";
    return line.str();
}

void BotRun::NoteBossKill(uint32 groupId, uint32 entry)
{
    if (m_phase == Phase::Script && groupId == m_groupId)
        m_bossKills.insert(entry);
}

void BotRun::SetPhase(Phase phase)
{
    m_phase = phase;
    m_phaseMs = SteadyMs();
}

void BotRun::Fail(std::string const& reason)
{
    Finish(false, reason);
}

void BotRun::Finish(bool passed, std::string const& reason)
{
    // 1. The result. A threat loss does not fail the run: the spec allows
    // some (spec section 10). The result counts them.
    m_passed = passed;
    std::ostringstream line;
    line << "Run " << m_number << " " << m_script.name << " " << m_scenario << ": "
         << (passed ? "passed" : "failed") << " after " << (SteadyMs() - m_startMs) / 1000 << " s";
    if (!passed)
        line << ": " << reason;
    line << WatchText();
    m_result = line.str();
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %s", m_result.c_str());
    // 2. The writers of the run end come here, before the group ends.
    // 3. The group waits for the chat of the run (Update, Phase::Closing).
    SetPhase(Phase::Closing);
}

bool BotRun::EndClosing(char const* cutBy)
{
    if (m_phase != Phase::Closing)
        return false;
    if (cutBy)
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: the wait for the chat of the run ends early: %s", m_number, cutBy);
    EndRunGroup();
    m_phase = m_passed ? Phase::Passed : Phase::Failed;
    return true;
}

void BotRun::EndRunGroup()
{
    // The test driver is a normal bot again.
    sBotMgr.SetTestDriver(ObjectGuid());
    uint32 const groupId = m_groupId;
    m_groupId = 0;
    Group* group = groupId ? sObjectMgr.GetGroupById(groupId) : nullptr;
    if (!group)
        return;
    std::vector<ObjectGuid> members;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
        members.push_back(slot.guid);
    // The last but one leave ends the group: the core deletes it, in the
    // game and in the database.
    for (ObjectGuid const& member : members)
        if (Group* left = sObjectMgr.GetGroupById(groupId))
            Player::RemoveFromGroup(left, member);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: the members leave the run group %u", m_number, groupId);
}

bool BotRun::PhaseTimedOut(std::string const& what)
{
    if (SteadyMs() - m_phaseMs < ResetPhaseLimitMs)
        return false;
    Fail(std::string(PhaseName(int(m_phase))) + ": " + what + " in " + std::to_string(ResetPhaseLimitMs / 1000) + " s");
    return true;
}

Player* BotRun::Driver() const
{
    return FindByName(m_script.driver);
}

std::vector<Player*> BotRun::Members() const
{
    // The driver first.
    std::vector<Player*> members;
    if (Player* driver = Driver())
        members.push_back(driver);
    for (std::string const& name : m_script.group)
        if (Player* bot = FindByName(name))
            members.push_back(bot);
    return members;
}

bool BotRun::AllOnline() const
{
    std::vector<std::string> names = m_script.group;
    names.push_back(m_script.driver);
    for (std::string const& name : names)
        if (sBotMgr.OnlineStateOf(name) != BotOnlineState::Online || !InWorldAndStill(FindByName(name)))
            return false;
    return true;
}

bool BotRun::AllOffline() const
{
    std::vector<std::string> names = m_script.group;
    names.push_back(m_script.driver);
    for (std::string const& name : names)
        if (sBotMgr.OnlineStateOf(name) != BotOnlineState::Offline)
            return false;
    return true;
}

void BotRun::Update()
{
    std::vector<std::string> names = m_script.group;
    names.push_back(m_script.driver);
    std::string error;
    switch (m_phase)
    {
        case Phase::Idle:
        case Phase::Passed:
        case Phase::Failed:
            return;
        case Phase::LoginFirst:
            for (std::string const& name : names)
                if (sBotMgr.OnlineStateOf(name) == BotOnlineState::Offline)
                    sBotMgr.LoginBot(name, error);
            if (!AllOnline())
            {
                PhaseTimedOut("not every member logged in");
                return;
            }
            // Out of the dungeon, so that the next entry makes a new
            // instance: the core does not unbind the map a player is on.
            for (Player* member : Members())
            {
                if (!member->IsAlive())
                {
                    member->ResurrectPlayer(1.0f);
                    member->SpawnCorpseBones();
                }
                member->CombatStop(true);
                if (member->GetMapId() != m_script.outsideMap ||
                    !member->IsWithinDist3d(m_script.outside.x, m_script.outside.y, m_script.outside.z, 20.0f))
                    member->TeleportTo(m_script.outsideMap, m_script.outside.x + frand(-2.0f, 2.0f),
                        m_script.outside.y + frand(-2.0f, 2.0f), m_script.outside.z + 0.5f, 0.0f);
            }
            SetPhase(Phase::GoOutside);
            return;
        case Phase::GoOutside:
            for (Player* member : Members())
                if (!InWorldAndStill(member) || member->GetMapId() != m_script.outsideMap)
                {
                    PhaseTimedOut("not every member came out of the dungeon");
                    return;
                }
            if (Members().size() != names.size())
            {
                PhaseTimedOut("a member logged out");
                return;
            }
            for (std::string const& name : names)
                sBotMgr.LogoutBot(name, error);
            SetPhase(Phase::Logout);
            return;
        case Phase::Logout:
            if (!AllOffline())
            {
                PhaseTimedOut("not every member logged out");
                return;
            }
            for (std::string const& name : names)
                sBotMgr.LoginBot(name, error);
            SetPhase(Phase::Login);
            return;
        case Phase::Login:
            if (!AllOnline())
            {
                PhaseTimedOut("not every member logged in again");
                return;
            }
            if (!FormGroup(error))
            {
                Fail("reset: " + error);
                return;
            }
            Prepare();
            SetPhase(Phase::Script);
            return;
        case Phase::Script:
            break;
        case Phase::Closing:
            if (Bots::RunEndWaitsForChat(SteadyMs() - m_phaseMs, m_groupId && sBotMgr.Chat().HasPendingChat(m_groupId)))
                return;
            EndClosing(nullptr);
            return;
    }

    if (m_teleportRest)
        TeleportRest();
    ReviveTheDead();
    WatchThreat(SteadyMs() - m_startMs);
    if (m_phase != Phase::Script)
        return;
    for (;;)
    {
        // The facts again after each command: a teleport changes them.
        int64 const now = SteadyMs() - m_startMs;
        Bots::RunStep const step = m_runner->Update(Facts(), now);
        if (step.kind == Bots::RunStepKind::Wait)
            return;
        if (step.kind == Bots::RunStepKind::Failed)
        {
            Fail(step.reason);
            return;
        }
        if (step.kind == Bots::RunStepKind::Passed)
        {
            Finish(true, std::string());
            return;
        }
        std::string detail;
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: line %u: %s", m_number, uint32(step.command.line),
            step.command.source.c_str());
        bool const ok = Do(step.command, detail);
        if (ok && !detail.empty())
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s", m_number, detail.c_str());
        m_runner->Done(ok, detail, now);
    }
}

bool BotRun::FormGroup(std::string& error)
{
    Player* driver = Driver();
    std::vector<Player*> bots;
    for (std::string const& name : m_script.group)
        if (Player* bot = FindByName(name))
            bots.push_back(bot);
    if (!driver || bots.size() != m_script.group.size())
    {
        error = "a member is not in the world";
        return false;
    }

    Group* group = driver->GetGroup();
    bool right = group && group->IsLeader(driver->GetObjectGuid()) && group->GetMembersCount() == bots.size() + 1;
    for (Player* bot : bots)
        right = right && bot->GetGroup() == group;
    if (right)
    {
        m_groupId = group->GetId();
        return true;
    }

    // A new group, with the driver as its leader and only the bots.
    std::vector<Player*> members = bots;
    members.push_back(driver);
    for (Player* member : members)
        if (Group* old = member->GetGroup())
            Player::RemoveFromGroup(old, member->GetObjectGuid());
    group = new Group;
    if (!group->Create(driver->GetObjectGuid(), driver->GetName()))
    {
        delete group;
        error = "cannot make the group of the test driver";
        return false;
    }
    sObjectMgr.AddGroup(group);
    // From here on, the end of the run ends this group.
    m_groupId = group->GetId();
    for (Player* bot : bots)
        if (!group->AddMember(bot->GetObjectGuid(), bot->GetName()))
        {
            error = std::string("cannot add ") + bot->GetName() + " to the group";
            return false;
        }
    group->BroadcastGroupUpdate();
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: new group %u of %s", m_number, group->GetId(), driver->GetName());
    return true;
}

void BotRun::Prepare()
{
    Group* group = Driver()->GetGroup();
    // A new instance: no member and no group keeps the old one.
    group->UnbindInstance(m_route.mapId);
    for (Player* member : Members())
        member->UnbindInstance(m_route.mapId);
    // A fresh coordinator: no pull, focus, boss fight, or wipe of the last run.
    sBotMgr.DropCoordinator(m_groupId);
    for (Player* member : Members())
        ResetMember(member);
}

void BotRun::ResetMember(Player* member)
{
    if (!member->IsAlive())
    {
        member->ResurrectPlayer(1.0f);
        member->SpawnCorpseBones();
    }
    member->CombatStop(true);
    member->SetSelectionGuid(ObjectGuid());
    // Level 16 with zero experience, then the level routine (with the gear).
    uint32 const level = Bots::Version1BotLevel;
    if (member->GetLevel() != level)
    {
        member->GiveLevel(level);
        member->InitTalentForLevel();
    }
    member->SetUInt32Value(PLAYER_XP, 0);
    RemoveCastAuras(member);
    uint32 const destroyed = EmptyBags(member);

    bool const driver = member == Driver();
    std::string report, error;
    if (!sBotMgr.EquipCharacter(member->GetName(), driver ? DriverGear : "", report, error))
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] Run %u: %s", m_number, error.c_str());
    sBotMgr.ApplyLevelRoutine(member);
    member->DurabilityRepairAll(false, 0.0f);
    member->RemoveAllCooldowns();
    FullHealthAndPower(member);
    if (Pet* pet = member->GetPet())
    {
        RemoveCastAuras(pet);
        pet->CombatStop(true);
        pet->RemoveAllCooldowns();
        FullHealthAndPower(pet);
    }
    member->SetStandState(UNIT_STAND_STATE_STAND);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: reset %s: level %u, xp %u, %u bag items destroyed, health %u/%u, mana %u/%u. %s",
        m_number, member->GetName(), member->GetLevel(), member->GetUInt32Value(PLAYER_XP), destroyed, member->GetHealth(), member->GetMaxHealth(),
        member->GetPower(POWER_MANA), member->GetMaxPower(POWER_MANA), report.c_str());
}

Bots::RunFacts BotRun::Facts() const
{
    Bots::RunFacts facts;
    Player* driver = Driver();
    std::vector<Player*> const members = Members();
    facts.teleporting = m_teleportRest || !driver || members.size() != m_script.group.size() + 1;
    for (Player* member : members)
    {
        if (!InWorldAndStill(member) || (driver && member->GetMapId() != driver->GetMapId()))
            facts.teleporting = true;
        if (member->IsInWorld() && member->IsAlive() && member->IsInCombat())
            facts.groupInCombat = true;
        if (Pet* pet = member->GetPet())
            if (pet->IsAlive() && pet->IsInCombat())
                facts.groupInCombat = true;
    }
    if (m_moving && driver && driver->IsInWorld())
    {
        float const dx = driver->GetPositionX() - m_moveX, dy = driver->GetPositionY() - m_moveY;
        facts.driverArrived = std::sqrt(dx * dx + dy * dy) <= m_moveRadius + ArriveSlackYards;
    }
    return facts;
}

void BotRun::WatchThreat(int64 nowMs)
{
    std::vector<Bots::EnemyOnMember> hitsOnNonTanks, hits;
    std::vector<Bots::MemberAttacks> attacks;
    Bots::Guid holder = Bots::NoGuid;
    Player const* driver = Driver();
    for (Player* member : Members())
    {
        if (!IsLivingMember(member))
            continue;
        attacks.push_back(BotAdapter::AttacksOf(member, member != driver));
        Bots::BotRole role;
        bool const tank = sBotMgr.BotRoleOf(member, role) && role == Bots::BotRole::Tank;
        if (tank && member->GetVictim())
            holder = member->GetObjectGuid().GetRawValue();
        for (Unit* attacker : member->GetAttackers())
            if (attacker->IsAlive() && attacker->GetVictim() == member)
            {
                Bots::EnemyOnMember const hit{attacker->GetObjectGuid().GetRawValue(), member->GetObjectGuid().GetRawValue()};
                hits.push_back(hit);
                if (!tank)
                    hitsOnNonTanks.push_back(hit);
            }
    }

    std::size_t const known = m_threat.Losses().size();
    m_threat.Update(hitsOnNonTanks, nowMs);
    for (std::size_t i = known; i < m_threat.Losses().size(); ++i)
    {
        Bots::ThreatLoss const& loss = m_threat.Losses()[i];
        m_threatNames.push_back(HitName(loss.enemy, loss.member));
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: threat loss: %s for more than %u s", m_number,
            m_threatNames.back().c_str(), uint32(Bots::ThreatLossMs / 1000));
    }

    std::size_t const lostBefore = m_lostMobs.LostMobs().size();
    m_lostMobs.Update(hits, Bots::AttackedEnemies(attacks), nowMs, holder);
    for (std::size_t i = 0; i < m_lostMobs.LostMobs().size(); ++i)
    {
        Bots::LostMob const& mob = m_lostMobs.LostMobs()[i];
        if (i >= m_lostMobNames.size())
            m_lostMobNames.push_back(HitName(mob.enemy, mob.member));
    }
    if (m_lostMobs.LostMobs().size() > lostBefore)
    {
        Bots::LostMob const& mob = m_lostMobs.LostMobs()[lostBefore];
        Fail("lost mob: " + m_lostMobNames[lostBefore] + ", and no bot attacks it for more than "
            + std::to_string((mob.endMs - mob.startMs) / 1000) + " s");
    }
}

std::string BotRun::HitName(Bots::Guid enemy, Bots::Guid member) const
{
    std::string enemyName = "an enemy", memberName = "a member";
    for (Player* player : Members())
        if (player->GetObjectGuid().GetRawValue() == member)
        {
            memberName = player->GetName();
            if (Unit* unit = player->GetMap()->GetUnit(ObjectGuid(enemy)))
                enemyName = unit->GetName();
        }
    return enemyName + " on " + memberName;
}

std::string BotRun::WatchText() const
{
    std::ostringstream text;
    std::vector<Bots::ThreatLoss> const& losses = m_threat.Losses();
    text << ", threat losses " << losses.size();
    for (std::size_t i = 0; i < losses.size(); ++i)
        text << (i ? "; " : " (") << m_threatNames[i] << " " << (losses[i].endMs - losses[i].startMs) / 1000 << " s"
             << (i + 1 == losses.size() ? ")" : "");
    std::vector<Bots::LostMob> const& lost = m_lostMobs.LostMobs();
    text << ", lost mobs " << lost.size();
    for (std::size_t i = 0; i < lost.size(); ++i)
        text << (i ? "; " : " (") << m_lostMobNames[i] << " " << (lost[i].endMs - lost[i].startMs) / 1000 << " s"
             << (i + 1 == lost.size() ? ")" : "");
    return text.str();
}

bool BotRun::Do(Bots::ScriptCommand const& command, std::string& detail)
{
    Player* driver = Driver();
    if (!InWorldAndStill(driver))
    {
        detail = "the test driver is not in the world";
        return false;
    }
    switch (command.kind)
    {
        case Bots::ScriptCommandKind::Teleport:
            return Teleport(*Bots::FindPoint(m_route, command.name), detail);
        case Bots::ScriptCommandKind::Move:
            return Move(*Bots::FindPoint(m_route, command.name), detail);
        case Bots::ScriptCommandKind::Command:
            return Say(CHAT_MSG_PARTY, nullptr, command.text);
        case Bots::ScriptCommandKind::Whisper:
        {
            Player* bot = FindByName(command.name);
            if (!bot)
            {
                detail = command.name + " is not in the world";
                return false;
            }
            return Say(CHAT_MSG_WHISPER, bot, command.text);
        }
        case Bots::ScriptCommandKind::Target:
            return Target(command, detail);
        case Bots::ScriptCommandKind::Clear:
            return Clear(command, detail);
        case Bots::ScriptCommandKind::Refill:
            Refill();
            return true;
        case Bots::ScriptCommandKind::ExpectBossDead:
            return ExpectBossDead(command.entry, detail);
        case Bots::ScriptCommandKind::ExpectPackDead:
            return ExpectPackDead(command.name, detail);
        case Bots::ScriptCommandKind::ExpectAlive:
            return ExpectAlive(detail);
        default:
            detail = "the runner waits for this command";
            return false;
    }
}

Bots::RoutePlace BotRun::PickPlace(Bots::RoutePoint const& point, std::string const& who, float& x, float& y)
{
    std::size_t const choice = urand(0, uint32(point.places.size() - 1));
    Bots::RoutePlace const& place = point.places[choice];
    float const angle = frand(0.0f, 2.0f * float(M_PI));
    float const distance = place.radius * std::sqrt(frand(0.0f, 1.0f));
    x = place.position.x + distance * std::cos(angle);
    y = place.position.y + distance * std::sin(angle);
    // A random choice of the run.
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s goes to %s, place %u of %u, at %.2f %.2f", m_number,
        who.c_str(), point.name.c_str(), uint32(choice + 1), uint32(point.places.size()), x, y);
    return place;
}

bool BotRun::Teleport(Bots::RoutePoint const& point, std::string& detail)
{
    // The driver first: its entry makes the instance and binds the group,
    // so that the bots come into the same one.
    Player* driver = Driver();
    float x, y;
    Bots::RoutePlace const place = PickPlace(point, driver->GetName(), x, y);
    if (!driver->TeleportTo(m_route.mapId, x, y, place.position.z + 0.5f, driver->GetOrientation()))
    {
        detail = "the test driver cannot go to the map " + std::to_string(m_route.mapId);
        return false;
    }
    m_moving = false;
    m_teleportRest = true;
    m_teleportPoint = point;
    return true;
}

void BotRun::TeleportRest()
{
    Player* driver = Driver();
    if (!InWorldAndStill(driver) || driver->GetMapId() != m_route.mapId)
        return;
    m_teleportRest = false;
    for (std::string const& name : m_script.group)
        if (Player* bot = FindByName(name))
        {
            float x, y;
            Bots::RoutePlace const place = PickPlace(m_teleportPoint, name, x, y);
            bot->TeleportTo(m_route.mapId, x, y, place.position.z + 0.5f, driver->GetOrientation());
        }
}

bool BotRun::Move(Bots::RoutePoint const& point, std::string& detail)
{
    Player* driver = Driver();
    if (driver->GetMapId() != m_route.mapId)
    {
        detail = "the test driver is not in the dungeon";
        return false;
    }
    float x, y;
    Bots::RoutePlace const place = PickPlace(point, driver->GetName(), x, y);
    float z = place.position.z;
    driver->UpdateAllowedPositionZ(x, y, z);
    driver->GetMotionMaster()->Clear();
    driver->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING | MOVE_RUN_MODE);
    m_moving = true;
    m_moveX = x;
    m_moveY = y;
    m_moveRadius = place.radius;
    return true;
}

bool BotRun::Say(uint32 chatType, Player* receiver, std::string const& text)
{
    // The chat of a player, so that a GM in the group sees it too, then the
    // hook of the player's chat: parsing, routing, and chat events are the
    // same code as for the player.
    Player* driver = Driver();
    WorldPacket data;
    ChatHandler::BuildChatPacket(data, ChatMsg(chatType), text.c_str(), LANG_UNIVERSAL, driver->GetChatTag(),
        driver->GetObjectGuid(), driver->GetName());
    if (chatType == CHAT_MSG_PARTY)
    {
        if (Group* group = driver->GetGroup())
            group->BroadcastPacket(&data, false, group->GetMemberGroup(driver->GetObjectGuid()));
    }
    else if (receiver)
        receiver->GetSession()->SendPacket(&data);
    BotHooks::OnPlayerChat(driver, chatType, receiver, text);
    return true;
}

bool BotRun::Target(Bots::ScriptCommand const& command, std::string& detail)
{
    Player* driver = Driver();
    Creature* target = nullptr;
    if (command.entry)
        target = driver->FindNearestCreature(command.entry, SearchYards, true);
    else
        for (Bots::RouteMob const& mob : Bots::FindPack(m_route, command.name)->mobs)
        {
            Creature* creature = SpawnOf(driver, mob);
            if (creature && creature->IsAlive() && (!target || driver->GetDistance(creature) < driver->GetDistance(target)))
                target = creature;
        }
    if (!target)
    {
        detail = "no living target near the test driver";
        return false;
    }
    driver->SetSelectionGuid(target->GetObjectGuid());
    detail = std::string("the test driver selects ") + target->GetName() + " " + target->GetObjectGuid().GetString();
    return true;
}

bool BotRun::Clear(Bots::ScriptCommand const& command, std::string& detail)
{
    Player* driver = Driver();
    std::set<uint32> keep;
    if (!command.name.empty())
        for (Bots::RouteMob const& mob : Bots::FindPack(m_route, command.name)->mobs)
            keep.insert(mob.spawnGuid);

    std::list<Creature*> creatures;
    LivingNearCheck check = {driver, command.yards};
    MaNGOS::CreatureListSearcher<LivingNearCheck> searcher(creatures, check);
    Cell::VisitGridObjects(driver, searcher, command.yards);
    uint32 cleared = 0;
    for (Creature* creature : creatures)
    {
        if (creature->IsPet() || creature->IsTotem() || !creature->IsHostileTo(driver) ||
            sBotMgr.IsBoss(driver->GetMapId(), creature->GetEntry()) || keep.count(creature->GetDBTableGUIDLow()))
            continue;
        creature->ForcedDespawn();
        ++cleared;
    }
    detail = std::to_string(cleared) + " creatures went away";
    return true;
}

void BotRun::Refill()
{
    for (Player* member : Members())
    {
        if (!member->IsAlive())
            continue;
        FullHealthAndPower(member);
        if (Pet* pet = member->GetPet())
            FullHealthAndPower(pet);
    }
}

bool BotRun::ExpectBossDead(uint32 entry, std::string& detail)
{
    if (m_bossKills.count(entry))
    {
        detail = "boss_kill " + std::to_string(entry) + " came from the group";
        return true;
    }
    Player* driver = Driver();
    if (Creature* boss = driver->FindNearestCreature(entry, SearchYards, true))
    {
        detail = std::string(boss->GetName()) + " lives with " + std::to_string(boss->GetHealth()) + " of " +
            std::to_string(boss->GetMaxHealth()) + " health";
        return false;
    }
    if (driver->FindNearestCreature(entry, SearchYards, false))
    {
        detail = "the boss " + std::to_string(entry) + " is dead (no boss_kill event)";
        return true;
    }
    detail = "no creature " + std::to_string(entry) + " near the test driver";
    return false;
}

bool BotRun::ExpectPackDead(std::string const& pack, std::string& detail)
{
    Player* driver = Driver();
    for (Bots::RouteMob const& mob : Bots::FindPack(m_route, pack)->mobs)
        if (Creature* creature = SpawnOf(driver, mob))
            if (creature->IsAlive())
            {
                detail = std::string(creature->GetName()) + " " + std::to_string(mob.spawnGuid) + " lives";
                return false;
            }
    return true;
}

bool BotRun::ExpectAlive(std::string& detail)
{
    for (Player* member : Members())
        if (!member->IsAlive())
        {
            detail = std::string(member->GetName()) + " is dead";
            return false;
        }
    return true;
}

// Database: ResurrectPlayer and the teleport change only the in-memory
// player; the next autosave or the logout at the end of the run writes them
// (a bot runs with the normal player save). SpawnCorpseBones deletes the
// corpse row at once. Client: a bot has no client; real players near the
// member see the resurrect and the move through the normal update packets.
void BotRun::ReviveAt(Player* member, uint32 mapId, float x, float y, float z, float o)
{
    member->ResurrectPlayer(0.5f);
    member->SpawnCorpseBones();
    member->TeleportTo(mapId, x, y, z, o);
}

void BotRun::ReviveTheDead()
{
    std::vector<Player*> const members = Members();
    std::vector<Player*> living;
    for (Player* member : members)
        if (IsLivingMember(member))
            living.push_back(member);

    // The wipe rule for a member away or in a teleport: WipeRevive.h.
    std::vector<std::string> names = m_script.group;
    names.insert(names.begin(), m_script.driver);
    std::vector<Bots::RunMember> states;
    int away = 0;
    for (std::string const& name : names)
    {
        Player const* member = FindByName(name);
        if (!member || !member->IsInWorld())
        {
            states.push_back(Bots::RunMember::Away);
            ++away;
        }
        else if (member->IsBeingTeleported())
            states.push_back(Bots::RunMember::InTeleport);
        else
            states.push_back(member->IsAlive() ? Bots::RunMember::Living : Bots::RunMember::Dead);
    }
    if (m_wipeWatch.Update(states, SteadyMs()))
    {
        AreaTriggerTeleport const* entrance = sObjectMgr.GetMapEntranceTrigger(m_route.mapId);
        if (!entrance)
            return;
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
            "[Bots] Run %u: wipe: everyone revives at the dungeon entrance (%d not in the world)", m_number, away);
        for (Player* member : members)
            if (InWorldAndStill(member))
                ReviveAt(member, entrance->destination.mapId, entrance->destination.x + frand(-2.0f, 2.0f),
                    entrance->destination.y + frand(-2.0f, 2.0f), entrance->destination.z, entrance->destination.o);
        return;
    }
    if (living.empty() || Facts().groupInCombat)
        return;

    // Only a released member: a dead body waits for the Priest's resurrect.
    for (Player* member : members)
    {
        Corpse const* corpse = member->GetCorpse();
        if (!InWorldAndStill(member) || member->IsAlive() || !member->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) || !corpse)
            continue;
        Player* nearest = nullptr;
        for (Player* other : living)
            if (other->GetMapId() == corpse->GetMapId() && (!nearest ||
                other->GetDistance(corpse) < nearest->GetDistance(corpse)))
                nearest = other;
        if (!nearest)
            nearest = living.front();
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s revives at %s", m_number, member->GetName(), nearest->GetName());
        ReviveAt(member, nearest->GetMapId(), nearest->GetPositionX(), nearest->GetPositionY(), nearest->GetPositionZ(),
            nearest->GetOrientation());
    }
}
