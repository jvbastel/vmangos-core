/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "BotRun.h"
#include "Bag.h"
#include "BotAdapter.h"
#include "BotGeometry.h"
#include "BotHooks.h"
#include "BotMgr.h"
#include "BotRecipe.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Corpse.h"
#include "Creature.h"
#include "CreatureGroups.h"
#include "ExtraPackWatch.h"
#include "FightSpot.h"
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
#include "PointMovementGenerator.h"
#include "GroupRules.h"
#include "RouteFiles.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "WipeRevive.h"
#include "Utilities/Random.h"
#include "WorldSession.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <list>
#include <sstream>
#include <sys/stat.h>
#if PLATFORM == PLATFORM_WINDOWS
#include <direct.h>
#endif

namespace
{
    // The fixed gear template of the test driver.
    char const* const DriverGear = "test_driver_mage";
    // A move of the test driver shorter than this is no progress.
    float const ProgressYards = 1.0f;
    // The longest time of one reset phase: the logins, the logouts, and the
    // teleport out of the dungeon.
    int64 const ResetPhaseLimitMs = 60000;
    // The test driver finds the creatures of `target` and `expect` within
    // this distance.
    float const SearchYards = 300.0f;
    // The test driver arrived when it is this much past the radius of the
    // place of its `move`.
    float const ArriveSlackYards = 1.5f;

    // The motion that took the test driver off its scripted move, for the
    // stop reason when no rule of its bot AI did it.
    std::string MotionName(MovementGeneratorType type)
    {
        switch (type)
        {
            case IDLE_MOTION_TYPE: return "idle";
            case CONFUSED_MOTION_TYPE: return "confused";
            case CHASE_MOTION_TYPE: return "chase";
            case POINT_MOTION_TYPE: return "another point";
            case FLEEING_MOTION_TYPE:
            case TIMED_FLEEING_MOTION_TYPE: return "fleeing";
            case FOLLOW_MOTION_TYPE: return "follow";
            case EFFECT_MOTION_TYPE: return "effect";
            default: return "type " + std::to_string(int(type));
        }
    }

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

    // The character `name` in the world. `includeNotInWorld`: also out of
    // the world, for example during a far teleport.
    Player* FindByName(std::string const& name, bool includeNotInWorld = false)
    {
        std::string wanted = name;
        if (!normalizePlayerName(wanted))
            return nullptr;
        return includeNotInWorld ? ObjectAccessor::FindPlayerByNameNotInWorld(wanted.c_str())
                                 : ObjectAccessor::FindPlayerByName(wanted.c_str());
    }

    // The state of a run member (WipeRevive.h). A far teleport takes the
    // member out of the world until it lands: the teleport counts first.
    Bots::RunMember RunMemberOf(Player const* player)
    {
        if (player && player->IsBeingTeleported())
            return Bots::RunMember::InTeleport;
        if (!player || !player->IsInWorld())
            return Bots::RunMember::NotInWorld;
        return player->IsAlive() ? Bots::RunMember::Living : Bots::RunMember::Dead;
    }

    bool InWorldAndStill(Player const* player)
    {
        Bots::RunMember const state = RunMemberOf(player);
        return state == Bots::RunMember::Living || state == Bots::RunMember::Dead;
    }

    // A member that lives and stands in the world, for the revive of the
    // dead and the threat watch. A member in a teleport counts as neither
    // living nor dead.
    bool IsLivingMember(Player const* player)
    {
        return RunMemberOf(player) == Bots::RunMember::Living;
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

    // The UTC time in a strftime format.
    std::string UtcNow(char const* format)
    {
        std::time_t const now = std::time(nullptr);
        std::tm utc;
#if PLATFORM == PLATFORM_WINDOWS
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        char text[32];
        std::strftime(text, sizeof(text), format, &utc);
        return text;
    }

    bool WriteFile(std::string const& path, std::string const& text)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
        return bool(out);
    }

    // The trace of the running run, line by line as it goes: the watch
    // script reads it (jvbastel/WOWAI#98). A new run starts it empty.
    char const* const LiveTraceFile = "live.trace.txt";

    void MakeDir(std::string const& dir)
    {
#if PLATFORM == PLATFORM_WINDOWS
        _mkdir(dir.c_str());
#else
        mkdir(dir.c_str(), 0775);
#endif
    }

    // "<name> <guid counter>" of a unit, for the trace.
    std::string UnitText(Unit const* unit)
    {
        return std::string(unit->GetName()) + " " + std::to_string(unit->GetGUIDLow());
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
    // A route file edited by hand: its bosses count from the next update,
    // before the run's first fight.
    sBotMgr.ReloadBossList();
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
    Bots::ScriptScenario const& commands = *Bots::FindScenario(m_script, scenario);
    bool const fullRun = Bots::IsFullRun(commands);
    m_runner.reset(new Bots::ScriptRunner(Bots::ExpandRoute(commands.commands, m_route), m_route));
    m_startMs = SteadyMs();
    m_result.clear();
    m_bossKills.clear();
    m_threat = Bots::ThreatWatch();
    m_lostMobs = Bots::LostMobWatch(sBotMgr.GetSettings().lostMobMs);
    m_threatNames.clear();
    m_lostMobNames.clear();
    m_interrupts = Bots::InterruptWatch();
    m_chases = Bots::ChaseWatch();
    m_combatNotes.Drain();
    m_unitNames.clear();
    m_chatEvents.clear();
    m_groupId = 0;
    m_teleportRest = false;
    m_moving = false;
    m_visited = false;
    m_move = Bots::ScriptedMove();
    EndMove();
    m_aborted = false;
    m_judge.reset(new Bots::RunJudge(fullRun ? m_route.bosses : std::vector<uint32>(), 0));
    // A full run keeps its whole trace, for the walked path on the dungeon
    // map (jvbastel/WOWAI#152); another run its last 3 minutes.
    m_trace = Bots::RunTrace(fullRun ? Bots::TraceKeepWholeRun : Bots::TraceKeepMs);
    m_fight = Bots::FightWatch();
    {
        std::map<uint32, std::string> packOf;
        for (Bots::RoutePack const& pack : m_route.packs)
            for (Bots::RouteMob const& mob : pack.mobs)
                packOf[mob.spawnGuid] = pack.name;
        m_extraPacks = Bots::ExtraPackWatch(std::move(packOf));
    }
    MakeDir(sBotMgr.GetSettings().runDir);
    if (!WriteFile(sBotMgr.GetSettings().runDir + "/" + LiveTraceFile, ""))
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] cannot write %s/%s", sBotMgr.GetSettings().runDir.c_str(), LiveTraceFile);
    m_quests.reset(new Bots::QuestProgress(m_route));
    m_releaseMs = Bots::WipeReviveDelayMs;
    m_releaseNever = false;
    m_wipeWatch = Bots::WipeWatch();
    m_alive.clear();
    m_enemyHealth.clear();
    m_fleeing.clear();
    m_pullFled.clear();
    m_pullChoices = Bots::PullChoices();
    m_joinCause.clear();
    m_pullJudged = false;
    m_driverSeen = false;
    ++m_number;

    m_record = Bots::RunResult();
    m_record.number = m_number;
    m_record.chaseLimitYards = Bots::ChaseLimitYards;
    m_record.script = m_script.name;
    m_record.scenario = m_scenario;
    m_record.fullRun = fullRun;
    for (Bots::ScriptCommand const& command : commands.commands)
        if (command.kind == Bots::ScriptCommandKind::Route)
            m_record.quests = command.quests;
    m_record.startedAt = UtcNow("%Y-%m-%dT%H:%M:%SZ");
    m_record.stamp = UtcNow("%Y%m%d-%H%M%S");
    m_record.driver = m_script.driver;
    m_record.group = m_script.group;
    sBotMgr.SetTestDriver(sObjectMgr.GetPlayerGuidByName(m_script.driver));
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s %s starts, test driver %s", m_number, m_script.name.c_str(),
        m_scenario.c_str(), m_script.driver.c_str());
    m_trace.Add(0, "run " + std::to_string(m_number) + " " + m_script.name + " " + m_scenario + " starts");
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
    m_aborted = true;
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
    if (m_phase != Phase::Script || groupId != m_groupId || !m_bossKills.insert(entry).second)
        return;
    int64 const now = RunMs();
    m_judge->NoteBossKill(entry, now);
    CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
    m_record.bossKills.push_back({entry, info ? info->name : "", now});
    std::string const kill = "boss_kill " + std::to_string(entry) + (info ? std::string(" ") + info->name : "");
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s", m_number, kill.c_str());
    m_trace.Add(now, kill);
}

int64 BotRun::RunMs() const
{
    return SteadyMs() - m_startMs;
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
    // some (spec section 10). The result counts them. A fight that still
    // runs ends here, with what its members did so far.
    if (m_fight.Running())
        EndFight(RunMs());
    // A pull with an unwarned or an avoidable extra pack fails (Bots::
    // PullFailure, jvbastel/WOWAI#123). The result counts and shows it, but
    // it does not fail the run (user decision, 2026-10-27).
    std::size_t const failedPulls = Bots::FailedPulls(m_record.pulls);
    m_passed = passed;
    // The result line counts the healer's losses from the record.
    RecordHits();
    std::ostringstream line;
    line << "Run " << m_number << " " << m_script.name << " " << m_scenario << ": "
         << (passed ? "passed" : m_aborted ? "aborted" : "failed") << " after " << RunMs() / 1000 << " s";
    if (!passed)
        line << ": " << reason;
    line << ", bosses " << m_record.bossKills.size() << ", wipes " << m_record.wipes.size()
         << ", deaths " << m_record.deaths.size();
    if (failedPulls)
        line << ", failed pulls " << failedPulls;
    line << WatchText();
    m_result = line.str();
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %s", m_result.c_str());
    m_trace.Add(RunMs(), m_result);
    // The driver may rest again.
    EndMove();
    // 2. The result file, and the trace of a full run or of a run that did
    // not pass.
    m_record.movesStopped = m_move.StopCount();
    m_record.durationMs = RunMs();
    m_record.outcome = passed ? Bots::RunOutcome::Passed : m_aborted ? Bots::RunOutcome::Aborted : Bots::RunOutcome::Failed;
    m_record.reason = passed ? "" : reason;
    WriteResultFiles();
    // 3. The group waits for the chat of the run (Update, Phase::Closing).
    SetPhase(Phase::Closing);
}

bool BotRun::EndClosing(char const* cutBy)
{
    if (m_phase != Phase::Closing)
        return false;
    if (cutBy)
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: the wait for the chat of the run ends early: %s", m_number, cutBy);
    // The result file again, with the chat lines that came while the group
    // waited for them.
    if (!WriteResultJson())
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] Run %u: cannot write the result file again", m_number);
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
    // The lines of the last update, and the result line of a stop.
    WriteLiveTrace();
    // The combat notes count only in the script phase (WatchCombat).
    if (m_phase != Phase::Script)
        m_combatNotes.Drain();
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
            // The reset is no part of the time without progress.
            m_judge->NoteProgress(RunMs());
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
    WatchThreat(RunMs());
    if (m_phase != Phase::Script)
        return;
    WatchRun(RunMs());
    if (m_phase != Phase::Script)
        return;
    for (;;)
    {
        // The facts again after each command: a teleport changes them.
        int64 const now = RunMs();
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
            // A full run passes only at its last boss: a wipe on the last
            // pack ends the fight too.
            Bots::RunVerdict const verdict = m_judge->AtScriptEnd();
            Finish(verdict.kind == Bots::RunVerdictKind::Passed, verdict.reason);
            return;
        }
        std::string detail;
        std::string const what = "line " + std::to_string(step.command.line) + ": " + step.command.source;
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s", m_number, what.c_str());
        m_trace.Add(now, what);
        if (step.command.kind == Bots::ScriptCommandKind::Fight && InWorldAndStill(Driver()) &&
            !LivingMobOf(step.command.name))
        {
            // The pack died in an earlier fight.
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s is dead already", m_number, step.command.name.c_str());
            m_runner->NothingToDo();
            continue;
        }
        bool const ok = Do(step.command, detail);
        if (ok && !detail.empty())
        {
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s", m_number, detail.c_str());
            m_trace.Add(now, detail);
        }
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
    // The level of the script (default 16) with zero experience, then the
    // level routine (with the gear).
    uint32 const level = m_script.level;
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
    EquipReport equipped;
    if (!sBotMgr.EquipCharacter(member->GetName(), driver ? DriverGear : "", report, error, &equipped))
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] Run %u: %s", m_number, error.c_str());
    else
    {
        // A random choice of the run: the gear draw.
        Bots::RunGearDraw draw = {member->GetName(), equipped.gearTemplate, equipped.draw.shape, {}};
        for (Bots::GearPick const& pick : equipped.draw.picks)
        {
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(pick.entry);
            draw.items.push_back({Bots::GearPositionName(pick.position), pick.entry, proto ? proto->Name1 : ""});
        }
        m_record.gearDraws.push_back(draw);
    }
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
    facts.groupResting = GroupResting();
    for (Player* member : members)
        if (InWorldAndStill(member) && !member->IsAlive())
            facts.memberDead = true;
    // A pack whose fight ended but lives: the tank died and the pack reset.
    // A driver in a teleport cannot look: the runner fights again, and that
    // fight finds the pack dead (Update, NothingToDo).
    std::string const pack = m_runner ? m_runner->FightPack() : std::string();
    if (!pack.empty())
        facts.fightPackAlive = !InWorldAndStill(driver) || LivingMobOf(pack);
    if (driver && driver->IsInWorld())
        facts.driverAt = {driver->GetPositionX(), driver->GetPositionY(), driver->GetPositionZ()};
    facts.driverArrived = DriverArrived(driver);
    facts.driverMoveStop = m_move.StopReason();
    return facts;
}

bool BotRun::DriverArrived(Player const* driver) const
{
    if (!m_moving || !driver || !driver->IsInWorld())
        return false;
    Bots::Position const at{driver->GetPositionX(), driver->GetPositionY(), 0.0f};
    return Bots::Distance2d(at, {m_moveX, m_moveY, 0.0f}) <= m_moveRadius + ArriveSlackYards;
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
        bool const hasRole = sBotMgr.BotRoleOf(member, role);
        bool const tank = hasRole && role == Bots::BotRole::Tank;
        Bots::Guid const guid = member->GetObjectGuid().GetRawValue();
        if (sBotMgr.IsHealerBot(member) &&
            !Bots::Contains(m_record.healers, member->GetName()))
            m_record.healers.push_back(member->GetName());
        if (tank && member->GetVictim())
            holder = guid;
        for (Unit* attacker : member->GetAttackers())
            if (attacker->IsAlive() && attacker->GetVictim() == member)
            {
                Bots::EnemyOnMember const hit{attacker->GetObjectGuid().GetRawValue(), guid};
                hits.push_back(hit);
                if (!tank)
                    hitsOnNonTanks.push_back(hit);
            }
    }

    Bots::HitNamer const name = [this](Bots::EnemyOnMember const& hit) {
        std::pair<std::string, std::string> const names = HitNames(hit.enemy, hit.member);
        return names.first + " on " + names.second;
    };
    std::size_t const known = m_threat.Losses().size();
    m_threat.Update(hitsOnNonTanks, nowMs, name);
    for (std::size_t i = known; i < m_threat.Losses().size(); ++i)
    {
        Bots::ThreatLoss const& loss = m_threat.Losses()[i];
        m_threatNames.push_back(HitNames(loss.enemy, loss.member));
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: threat loss: %s for more than %u s", m_number,
            loss.name.c_str(), uint32(Bots::ThreatLossMs / 1000));
        m_trace.Add(nowMs, "threat loss: " + loss.name);
    }

    std::size_t const lostBefore = m_lostMobs.LostMobs().size();
    m_lostMobs.Update(hits, Bots::AttackedEnemies(attacks), nowMs, holder, name);
    for (std::size_t i = m_lostMobNames.size(); i < m_lostMobs.LostMobs().size(); ++i)
        m_lostMobNames.push_back(HitNames(m_lostMobs.LostMobs()[i].enemy, m_lostMobs.LostMobs()[i].member));
    if (m_lostMobs.LostMobs().size() > lostBefore)
    {
        Bots::LostMob const& mob = m_lostMobs.LostMobs()[lostBefore];
        Fail("lost mob: " + mob.name + ", and no bot attacks it for more than " + std::to_string(Bots::HitSeconds(mob)) + " s");
    }
}

std::pair<std::string, std::string> BotRun::HitNames(Bots::Guid enemy, Bots::Guid member) const
{
    std::string enemyName = "an enemy", memberName = "a member";
    for (Player* player : Members())
        if (player->GetObjectGuid().GetRawValue() == member)
        {
            memberName = player->GetName();
            if (Unit* unit = player->GetMap()->GetUnit(ObjectGuid(enemy)))
                enemyName = unit->GetName();
        }
    return {enemyName, memberName};
}

std::string BotRun::WatchText() const
{
    return Bots::HitListText("threat losses", m_threat.Losses()) + ", healer threat losses " +
        std::to_string(Bots::HealerThreatLosses(m_record)) + Bots::HitListText("lost mobs", m_lostMobs.LostMobs());
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
            // A `command pull` is a pull of the result, like a `fight`.
            if (command.text == "pull")
                RecordPull(command);
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
        case Bots::ScriptCommandKind::Fight:
            return Fight(command, detail);
        case Bots::ScriptCommandKind::Release:
            m_releaseNever = command.never;
            m_releaseMs = int64(command.seconds) * 1000;
            detail = command.never ? "in a wipe, nobody releases"
                                   : "in a wipe, the test driver releases after " + std::to_string(command.seconds) + " s";
            return true;
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
    m_record.choices.push_back({who, point.name, uint32(choice + 1), uint32(point.places.size()), x, y, RunMs()});
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
    EndMove();
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
    m_moveX = x;
    m_moveY = y;
    // The one ground height helper of the bots (BotAdapter::GetGround).
    m_moveZ =BotAdapter(driver).GetGround({x, y, place.position.z}).z;
    SendMove(driver);
    m_moving = true;
    m_moveRadius = place.radius;
    m_movePoint = point.name;
    m_visited = false;
    // While the move is in progress, the driver does not rest. Only a move
    // that stopped in a fight is sent again (WatchMove).
    EndMove();
    std::string const line = m_move.Start(driver->GetName(), point.name);
    m_moveInProgress = true;
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s", m_number, line.c_str());
    m_trace.Add(RunMs(), line);
    return true;
}

void BotRun::NoteDriverMoveRule(std::string const& rule)
{
    std::lock_guard<std::mutex> lock(m_moveRuleMutex);
    m_moveRule = rule;
}

bool BotRun::OnScriptedMove(Player* driver) const
{
    MotionMaster const* motion = driver->GetMotionMaster();
    if (motion->empty())
        return false;
    auto const point = dynamic_cast<PointMovementGenerator<Player> const*>(motion->GetCurrent());
    float x, y, z;
    return point && point->GetDestination(x, y, z) && x == m_moveX && y == m_moveY;
}

void BotRun::SendMove(Player* driver)
{
    // A driver that still sits stands up: a walk ends its meal.
    driver->SetStandState(UNIT_STAND_STATE_STAND);
    driver->GetMotionMaster()->Clear();
    driver->GetMotionMaster()->MovePoint(0, m_moveX, m_moveY, m_moveZ, MOVE_PATHFINDING | MOVE_RUN_MODE);
}

void BotRun::WatchMove(Player* driver, int64 nowMs)
{
    if (!m_move.InProgress())
        return;
    bool const inFight = Facts().groupInCombat;
    std::string line;
    if (DriverArrived(driver))
        line = m_move.Arrive();
    else if (m_move.Waits())
    {
        // A move that stopped in a fight goes on when the fight is over
        // (jvbastel/WOWAI#128). A rule that takes the driver off it again
        // is its next stop reason.
        if (driver->IsAlive() && m_move.FightEnded(inFight, nowMs))
        {
            {
                std::lock_guard<std::mutex> lock(m_moveRuleMutex);
                m_moveRule.clear();
            }
            SendMove(driver);
            line = m_move.GoOn();
        }
    }
    else if (!OnScriptedMove(driver))
    {
        // A rule of the driver's bot AI, else the core: for example a stun,
        // a death, or the end of a point move that fell short.
        std::string rule;
        {
            std::lock_guard<std::mutex> lock(m_moveRuleMutex);
            rule = m_moveRule;
        }
        line = m_move.Stop(!rule.empty() ? "the rule " + rule
            : "the core's motion change to " + MotionName(driver->GetMotionMaster()->GetCurrentMovementGeneratorType()),
            inFight);
    }
    if (line.empty())
        return;
    m_moveInProgress = m_move.InProgress();
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s", m_number, line.c_str());
    m_trace.Add(nowMs, line);
}

void BotRun::EndMove()
{
    m_move.End();
    m_moveInProgress = false;
    std::lock_guard<std::mutex> lock(m_moveRuleMutex);
    m_moveRule.clear();
}

bool BotRun::Say(uint32 chatType, Player* receiver, std::string const& text)
{
    // The chat of a player, so that a GM in the group sees it too, then the
    // hook of the player's chat: parsing, routing, and chat events are the
    // same code as for the player.
    Player* driver = Driver();
    m_trace.Add(RunMs(), std::string(driver->GetName()) + (chatType == CHAT_MSG_PARTY ? " says in party: " :
        " whispers to " + std::string(receiver ? receiver->GetName() : "nobody") + ": ") + text);
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
    for (std::string const& pack : command.packs)
        for (Bots::RouteMob const& mob : Bots::FindPack(m_route, pack)->mobs)
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
    if (Creature const* creature = LivingMobOf(pack))
    {
        detail = std::string(creature->GetName()) + " " + std::to_string(creature->GetDBTableGUIDLow()) + " lives";
        return false;
    }
    return true;
}

Creature* BotRun::LivingMobOf(std::string const& pack) const
{
    Player* driver = Driver();
    for (Bots::RouteMob const& mob : Bots::FindPack(m_route, pack)->mobs)
        if (Creature* creature = SpawnOf(driver, mob))
            if (creature->IsAlive())
                return creature;
    return nullptr;
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
    // Half health and mana is no reason to rest for a bot: it rests until
    // full before the next pull (`wait rested`).
    if (member->IsBot() && m_groupId)
    {
        std::shared_ptr<Bots::GroupCoordinator> coordinator = CoordinatorOf(member);
        Bots::Guid const guid = member->GetObjectGuid().GetRawValue();
        coordinator->SetRestUntilFull(guid);
        // A tank that died in its pull: the pull is over, else the puller
        // does not rest (it is on its way to a fight).
        if (coordinator->GetPull().puller == guid)
            coordinator->EndPull();
    }
    member->TeleportTo(mapId, x, y, z, o);
}

void BotRun::ReviveTheDead()
{
    // One lookup of each member, the driver first, for the wipe rule
    // (WipeRevive.h) and for the revive of the released.
    std::vector<std::string> names = m_script.group;
    names.insert(names.begin(), m_script.driver);
    std::vector<Player*> members;
    std::vector<Player*> living;
    std::vector<Bots::RunMember> states;
    int notInWorld = 0;
    for (std::string const& name : names)
    {
        Player* member = FindByName(name, true);
        Bots::RunMember const state = RunMemberOf(member);
        states.push_back(state);
        if (state == Bots::RunMember::NotInWorld)
            ++notInWorld;
        if (state == Bots::RunMember::Living || state == Bots::RunMember::Dead)
            members.push_back(member);
        if (state == Bots::RunMember::Living)
            living.push_back(member);
    }
    // `release`: the test driver releases this long after the wipe, or never.
    bool const revive = m_wipeWatch.Update(states, SteadyMs(), m_releaseNever ? Bots::NeverRevive : m_releaseMs);
    if (m_wipeWatch.Began())
        NoteWipe();
    if (revive)
    {
        AreaTriggerTeleport const* entrance = sObjectMgr.GetMapEntranceTrigger(m_route.mapId);
        if (!entrance)
            return;
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
            "[Bots] Run %u: wipe: the test driver releases, and everyone revives at the dungeon entrance (%d not in the world)",
            m_number, notInWorld);
        m_trace.Add(RunMs(), "the test driver releases, and everyone revives at the dungeon entrance");
        for (Player* member : members)
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
        if (member->IsAlive() || !member->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) || !corpse)
            continue;
        Player* nearest = nullptr;
        for (Player* other : living)
            if (other->GetMapId() == corpse->GetMapId() && (!nearest ||
                other->GetDistance(corpse) < nearest->GetDistance(corpse)))
                nearest = other;
        if (!nearest)
            nearest = living.front();
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s revives at %s", m_number, member->GetName(), nearest->GetName());
        m_trace.Add(RunMs(), std::string(member->GetName()) + " revives at " + nearest->GetName());
        ReviveAt(member, nearest->GetMapId(), nearest->GetPositionX(), nearest->GetPositionY(), nearest->GetPositionZ(),
            nearest->GetOrientation());
    }
}

void BotRun::NoteWipe()
{
    int64 const now = RunMs();
    m_judge->NoteWipe();
    m_record.wipes.push_back(now);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: wipe %u", m_number, uint32(m_record.wipes.size()));
    m_trace.Add(now, "wipe " + std::to_string(m_record.wipes.size()));
}

void BotRun::WatchRun(int64 nowMs)
{
    for (Player* member : Members())
    {
        // A member in a teleport counts as neither living nor dead.
        if (!InWorldAndStill(member))
            continue;
        bool& alive = m_alive.emplace(member->GetName(), true).first->second;
        if (alive && !member->IsAlive())
        {
            m_record.deaths.push_back({member->GetName(), nowMs});
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s dies", m_number, member->GetName());
            m_trace.Add(nowMs, std::string(member->GetName()) + " dies");
        }
        alive = member->IsAlive();
    }

    WatchEnemies(nowMs);
    WatchCombat(nowMs);

    // Movement along the route: the test driver walks to the point of its
    // `move`, and the bots follow. A teleport is no walk.
    Player* driver = Driver();
    if (InWorldAndStill(driver))
    {
        if (!m_driverSeen || !driver->IsWithinDist3d(m_driverX, m_driverY, m_driverZ, ProgressYards))
        {
            if (m_driverSeen && m_moving && !m_visited)
                m_judge->NoteProgress(nowMs);
            m_driverSeen = true;
            m_driverX = driver->GetPositionX();
            m_driverY = driver->GetPositionY();
            m_driverZ = driver->GetPositionZ();
        }
        if (m_moving && !m_visited && DriverArrived(driver))
        {
            m_visited = true;
            m_quests->OnVisit(m_movePoint);
        }
        WatchMove(driver, nowMs);
    }

    TraceMembers(nowMs);
    WatchHold(nowMs);
    WatchFight(nowMs);

    Bots::RunVerdict const verdict = m_judge->Judge(nowMs);
    if (verdict.kind == Bots::RunVerdictKind::Passed)
        Finish(true, std::string());
    else if (verdict.kind == Bots::RunVerdictKind::Failed)
        Fail(verdict.reason);
}

void BotRun::WatchEnemies(int64 nowMs)
{
    Player* driver = Driver();
    if (!InWorldAndStill(driver))
        return;
    Map* map = driver->GetMap();
    // The enemies that the group fights now.
    for (Player* member : Members())
    {
        if (!IsLivingMember(member) || member->GetMap() != map)
            continue;
        std::vector<Unit*> enemies(member->GetAttackers().begin(), member->GetAttackers().end());
        enemies.push_back(member->GetVictim());
        if (Pet* pet = member->GetPet())
            enemies.push_back(pet->GetVictim());
        for (Unit* enemy : enemies)
            if (enemy && enemy->IsAlive() && enemy->GetTypeId() == TYPEID_UNIT && !enemy->IsPet())
                m_enemyHealth.emplace(enemy->GetObjectGuid(), enemy->GetHealth());
    }
    for (auto it = m_enemyHealth.begin(); it != m_enemyHealth.end();)
    {
        Creature* enemy = map->GetCreature(it->first);
        if (!enemy)
        {
            // It went away, or the group left the map.
            it = m_enemyHealth.erase(it);
            continue;
        }
        if (!enemy->IsAlive())
        {
            // A creature that `clear` took away has no corpse: no kill.
            if (enemy->GetDeathState() != CORPSE)
            {
                it = m_enemyHealth.erase(it);
                continue;
            }
            m_judge->NoteProgress(nowMs);
            m_quests->OnKill(enemy->GetEntry());
            m_trace.Add(nowMs, UnitText(enemy) + " dies");
            it = m_enemyHealth.erase(it);
            continue;
        }
        // A flee: from fear, or at low health. The low-health flee
        // (Creature::DoFleeToGetAssistance) runs to the nearest ally with
        // MoveSeekAssistance (ASSISTANCE_MOTION_TYPE) when one is in reach,
        // else flees in place for a time (TIMED_FLEEING_MOTION_TYPE): both
        // are the flee, and the first is the one that brings a pack.
        MovementGeneratorType const motion = enemy->GetMotionMaster()->GetCurrentMovementGeneratorType();
        bool const flees = enemy->HasUnitState(UNIT_STATE_FLEEING) || motion == FLEEING_MOTION_TYPE
            || motion == TIMED_FLEEING_MOTION_TYPE || motion == ASSISTANCE_MOTION_TYPE;
        if (flees && m_fleeing.insert(it->first).second)
        {
            m_pullFled[it->first] = UnitText(enemy);
            std::string const line = Bots::FleeLine(UnitText(enemy),
                {enemy->GetPositionX(), enemy->GetPositionY(), enemy->GetPositionZ()});
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s", m_number, line.c_str());
            m_trace.Add(nowMs, line);
        }
        else if (!flees)
            m_fleeing.erase(it->first);
        // Damage dealt.
        if (enemy->GetHealth() < it->second)
            m_judge->NoteProgress(nowMs);
        it->second = enemy->GetHealth();
        ++it;
    }
}

void BotRun::TraceMembers(int64 nowMs)
{
    for (Player* member : Members())
    {
        if (!InWorldAndStill(member))
            continue;
        Bots::MemberState state;
        state.name = member->GetName();
        state.position = {member->GetPositionX(), member->GetPositionY(), member->GetPositionZ()};
        Unit const* target = member->GetMap()->GetUnit(member->GetTargetGuid());
        if (!target)
            target = member->GetVictim();
        if (target)
            state.target = UnitText(target);
        state.spell = 0;
        // The auto-repeat spell last: the wand shot of the healer between
        // its heals (jvbastel/WOWAI#133).
        for (CurrentSpellTypes type : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL})
            if (Spell const* spell = member->GetCurrentSpell(type))
            {
                state.spell = spell->m_spellInfo->Id;
                state.spellName = spell->m_spellInfo->SpellName[0];
                break;
            }
        m_trace.Observe(nowMs, state);
    }
}

std::shared_ptr<Bots::GroupCoordinator> BotRun::CoordinatorOf(Player const* member) const
{
    Player const* driver = Driver();
    Bots::LeaderPlace const leader = driver ? Bots::LeaderPlace::On({driver->GetMapId(), driver->GetInstanceId()})
                                            : Bots::LeaderPlace::Unknown();
    Bots::MapPlace const place = member ? Bots::MapPlace{member->GetMapId(), member->GetInstanceId()} : leader.place;
    return sBotMgr.GetCoordinator(m_groupId, place, leader, SteadyMs());
}

bool BotRun::GroupResting() const
{
    if (!m_groupId)
        return false;
    std::shared_ptr<Bots::GroupCoordinator> const coordinator = CoordinatorOf(Driver());
    bool resting = false;
    for (Bots::Guid bot : coordinator->GetResting())
    {
        // Only a bot that is still here. A bot that died, left the group,
        // or logged out while it rested no longer updates its rest: the
        // coordinator forgets it, or `wait rested` waits for it for good.
        Player const* player = ObjectAccessor::FindPlayer(ObjectGuid(bot));
        if (player && player->IsInWorld() && player->IsAlive() && player->GetGroup() &&
            player->GetGroup()->GetId() == m_groupId)
            resting = true;
        else if (!player || !player->IsBeingTeleported())
            coordinator->ForgetRest(bot);
    }
    return resting;
}

bool BotRun::Fight(Bots::ScriptCommand const& command, std::string& detail)
{
    // The driver selects a mob of the pack and calls the pull, as the player
    // does. The bots that follow it come to the fight.
    if (!Target(command, detail))
        return false;
    RecordPull(command);
    return Say(CHAT_MSG_PARTY, nullptr, "pull");
}

void BotRun::RecordPull(Bots::ScriptCommand const& command)
{
    Player const* driver = Driver();
    Unit const* target = driver->GetMap()->GetUnit(driver->GetTargetGuid());
    // The pack of a `fight`; none for a `command pull`.
    std::string const pack = command.kind == Bots::ScriptCommandKind::Fight ? command.name : "";
    m_record.pulls.push_back({pack, target ? target->GetName() : "", RunMs(), command.again > 0});
    // The pack of the pull for the extra packs: that of the target of a
    // `command pull` too.
    Creature const* creature = target ? target->ToCreature() : nullptr;
    std::string const pullPack = !pack.empty() || !creature ? pack : m_extraPacks.PackOf(creature->GetDBTableGUIDLow());
    m_extraPacks.Start(pullPack);
    MarkPullPacks(target, pullPack);
    // A new pull ends the fight of the last one.
    if (m_fight.Running())
        EndFight(RunMs());
    std::vector<std::pair<std::string, Bots::Guid>> members;
    for (Player const* member : Members())
        members.emplace_back(member->GetName(), member->GetObjectGuid().GetRawValue());
    m_fight.Start(uint32(m_record.pulls.size()), pack, command.again > 0, RunMs(), members);
}

void BotRun::WatchFight(int64 nowMs)
{
    if (!m_fight.Running())
        return;
    std::set<Bots::Guid> enemies;
    for (auto const& enemy : m_enemyHealth)
        enemies.insert(enemy.first.GetRawValue());
    std::vector<Bots::FightMemberSeen> seen;
    for (Player* member : Members())
    {
        if (!InWorldAndStill(member))
            continue;
        // The attacks of the threat watch, and the cast of the interrupts.
        Bots::MemberAttacks const attacks = BotAdapter::AttacksOf(member, true);
        Bots::CastInfo const cast = BotAdapter::CastOf(member);
        // The victim of the member itself: a pet that fights for a member
        // that only follows is no part of the member (jvbastel/WOWAI#93).
        Bots::FightMemberSeen state = {member->GetName(), attacks.victim, cast.spell, Bots::NoGuid, cast.kind};
        // The first spell target is that of the cast. With no cast, it is
        // that of an auto-repeat spell (a wand's Shoot): an attack.
        if (!attacks.spellTargets.empty())
        {
            if (cast.spell != Bots::NoSpell)
                state.spellTarget = attacks.spellTargets.front();
            else if (state.victim == Bots::NoGuid)
                state.victim = attacks.spellTargets.front();
        }
        seen.push_back(state);
    }
    WatchExtraPacks(nowMs);
    if (m_fight.Observe(nowMs, Facts().groupInCombat, enemies, seen))
        EndFight(nowMs);
}

void BotRun::WatchExtraPacks(int64 nowMs)
{
    Player* driver = Driver();
    if (m_record.pulls.empty() || !InWorldAndStill(driver))
        return;
    std::set<uint32> spawns;
    for (auto const& enemy : m_enemyHealth)
        if (Creature const* creature = driver->GetMap()->GetCreature(enemy.first))
            spawns.insert(creature->GetDBTableGUIDLow());
    std::size_t const known = m_extraPacks.ExtraPacks().size();
    m_extraPacks.Observe(nowMs, spawns);
    RecordExtraPacks();
    if (m_extraPacks.ExtraPacks().size() == known)
        return;
    // The cause of each new pack, as it joins. Then the marks of all the
    // extra packs of the pull again: a pull that kept a pack out counts only
    // when it brought no more extra packs than the pull did
    // (Bots::MarkExtraPack).
    for (std::size_t i = known; i < m_extraPacks.ExtraPacks().size(); ++i)
    {
        std::string const& name = m_extraPacks.ExtraPacks()[i].pack;
        m_joinCause[name] = CauseOf(name);
    }
    std::size_t const got = m_extraPacks.ExtraPacks().size();
    for (std::size_t i = 0; i < got; ++i)
    {
        std::string const name = m_extraPacks.ExtraPacks()[i].pack;
        Bots::ExtraPackMark const atPull = Bots::MarkExtraPack(m_pullChoices, name, got);
        m_extraPacks.Mark(name, Bots::JudgeExtraPack(m_pullJudged,
            m_pullChoices.searched.count(name) ? &atPull : nullptr, m_joinCause[name]));
    }
    RecordExtraPacks();
    for (std::size_t i = known; i < got; ++i)
    {
        Bots::RunExtraPack const& extra = m_extraPacks.ExtraPacks()[i];
        std::string const text = "extra pack " + extra.pack + " joins pull " + std::to_string(m_record.pulls.size()) +
            (extra.warned ? " (warned)" : " (no warn)") + (extra.avoidable ? ", avoidable: " : ", unavoidable: ") +
            extra.reason;
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s", m_number, text.c_str());
        m_trace.Add(nowMs, text);
    }
}

void BotRun::MarkPullPacks(Unit const* target, std::string const& pullPack)
{
    m_pullFled.clear();
    m_pullChoices = Bots::PullChoices();
    m_joinCause.clear();
    m_pullJudged = false;
    Player* driver = Driver();
    if (!target || !driver)
        return;
    Player* tank = nullptr;
    for (Player* member : Members())
    {
        Bots::BotRole role;
        if (IsLivingMember(member) && sBotMgr.BotRoleOf(member, role) && role == Bots::BotRole::Tank)
            tank = member;
    }
    if (!tank || tank->GetMap() != target->GetMap())
        return;
    // The world thread runs after the maps updated (World::Update), so the
    // adapter's queries of the map are safe here.
    BotAdapter const view(tank);
    std::vector<Bots::Position> const trail = CoordinatorOf(tank)->GetTrail();
    m_pullJudged = true;
    std::vector<Bots::RoutePackAtPull> packs;
    for (Bots::RoutePack const& pack : m_route.packs)
    {
        if (pack.name == pullPack)
            continue;
        std::vector<Bots::Guid> enemies;
        for (Bots::RouteMob const& mob : pack.mobs)
            if (Creature const* creature = SpawnOf(tank, mob))
                if (creature->IsAlive() && tank->IsWithinDistInMap(creature, Bots::FightSpotEnemyYards))
                    enemies.push_back(creature->GetObjectGuid().GetRawValue());
        // A pack out of the search gets no mark: Bots::JudgeExtraPack.
        if (!enemies.empty())
            packs.push_back({pack.name, enemies});
    }
    m_pullChoices = Bots::FindPullChoices(view, trail, target->GetObjectGuid().GetRawValue(), packs);
}

Bots::ExtraPackCause BotRun::CauseOf(std::string const& pack) const
{
    Bots::ExtraPackCause cause;
    Player const* driver = Driver();
    if (!driver)
        return cause;
    auto const it = std::find_if(m_route.packs.begin(), m_route.packs.end(),
        [&pack](Bots::RoutePack const& routePack) { return routePack.name == pack; });
    if (it == m_route.packs.end())
        return cause;
    for (Bots::RouteMob const& mob : it->mobs)
    {
        Creature* creature = SpawnOf(driver, mob);
        if (!creature)
            continue;
        // A patrol (jvbastel/WOWAI#108): the spawn walks waypoints, or
        // follows the leader of its group who does.
        Creature const* leader = creature;
        if (CreatureGroup const* group = creature->GetCreatureGroup())
            if (Creature const* groupLeader = creature->GetMap()->GetCreature(group->GetLeaderGuid()))
                leader = groupLeader;
        if (creature->GetDefaultMovementType() == WAYPOINT_MOTION_TYPE
            || leader->GetDefaultMovementType() == WAYPOINT_MOTION_TYPE)
            cause.patrol = true;
        // A flee: an enemy that fled in this pull is near the pack now.
        for (auto const& fled : m_pullFled)
            if (Creature const* enemy = creature->GetMap()->GetCreature(fled.first))
                if (enemy->IsWithinDistInMap(creature, Bots::FleeBringsYards))
                    cause.fleeing = fled.second;
    }
    return cause;
}

void BotRun::WatchHold(int64 nowMs)
{
    if (!m_groupId)
        return;
    // The coordinator counts on the clock of the bots.
    for (std::string const& text : m_hold.Observe(*CoordinatorOf(Driver()), SteadyMs()))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s", m_number, text.c_str());
        m_trace.Add(nowMs, text);
    }
}

void BotRun::RecordExtraPacks()
{
    if (!m_record.pulls.empty())
        m_record.pulls.back().extraPacks = m_extraPacks.ExtraPacks();
}

void BotRun::EndFight(int64 nowMs)
{
    m_record.fights.push_back(m_fight.End(nowMs));
    std::string const text = Bots::FightText(m_record.fights.back());
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s", m_number, text.c_str());
    m_trace.Add(nowMs, text);
}

void BotRun::WriteLiveTrace()
{
    std::vector<std::string> const lines = m_trace.TakeNew();
    if (lines.empty())
        return;
    std::ofstream out(sBotMgr.GetSettings().runDir + "/" + LiveTraceFile, std::ios::binary | std::ios::app);
    for (std::string const& line : lines)
        out << line << "\n";
}

void BotRun::WriteResultFiles()
{
    if (m_quests)
        m_record.questProgress = m_quests->Objectives();
    RecordHits();
    RecordCombat();

    std::string const dir = sBotMgr.GetSettings().runDir;
    MakeDir(dir);
    std::string const stem = Bots::RunFileStem(m_record);
    std::string const path = dir + "/" + stem;
    m_record.traceFile.clear();
    if (m_record.fullRun || m_record.outcome != Bots::RunOutcome::Passed)
    {
        if (WriteFile(path + ".trace.txt", m_trace.Text()))
            m_record.traceFile = stem + ".trace.txt";
        else
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] Run %u: cannot write %s.trace.txt", m_number, path.c_str());
    }
    if (!WriteResultJson())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Bots] Run %u: cannot write %s.json", m_number, path.c_str());
        return;
    }
    m_result += ". Result: " + stem + ".json";
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: result %s.json%s", m_number, path.c_str(),
        m_record.traceFile.empty() ? "" : (", trace " + m_record.traceFile).c_str());
}

bool BotRun::WriteResultJson() const
{
    return WriteFile(sBotMgr.GetSettings().runDir + "/" + Bots::RunFileStem(m_record) + ".json", Bots::RunResultJson(m_record));
}

void BotRun::RecordHits()
{
    m_record.threatLosses.clear();
    for (std::size_t i = 0; i < m_threat.Losses().size(); ++i)
        m_record.threatLosses.push_back({m_threatNames[i].first, m_threatNames[i].second, m_threat.Losses()[i].startMs,
            m_threat.Losses()[i].endMs});
    m_record.lostMobs.clear();
    for (std::size_t i = 0; i < m_lostMobs.LostMobs().size(); ++i)
        m_record.lostMobs.push_back({m_lostMobNames[i].first, m_lostMobNames[i].second, m_lostMobs.LostMobs()[i].startMs,
            m_lostMobs.LostMobs()[i].endMs});
}

void BotRun::RecordCombat()
{
    auto const spellName = [](uint32 spell) {
        SpellEntry const* entry = sSpellMgr.GetSpellEntry(spell);
        return entry ? std::string(entry->SpellName[0]) : std::string();
    };
    m_record.interrupts.clear();
    for (Bots::InterruptDone const& done : m_interrupts.Done())
        m_record.interrupts.push_back({NameOf(ObjectGuid(done.member)), NameOf(ObjectGuid(done.enemy)), done.spell,
            spellName(done.spell), done.kind, done.ms});
    m_record.missedCasts.clear();
    for (Bots::MissedCast const& missed : m_interrupts.Missed())
        m_record.missedCasts.push_back({NameOf(ObjectGuid(missed.enemy)), missed.spell, spellName(missed.spell),
            missed.kind, missed.ms});
    m_record.doubleInterrupts.clear();
    for (Bots::DoubleInterrupt const& twice : m_interrupts.Doubles())
        m_record.doubleInterrupts.push_back({NameOf(ObjectGuid(twice.enemy)), twice.spell, spellName(twice.spell),
            NameOf(ObjectGuid(twice.first)), NameOf(ObjectGuid(twice.second)), twice.ms});
    m_record.chases.clear();
    for (Bots::FarChase const& chase : m_chases.Chases())
        m_record.chases.push_back({NameOf(ObjectGuid(chase.member)), NameOf(ObjectGuid(chase.enemy)), chase.startMs,
            chase.endMs, chase.maxYards});
}

void BotRun::WatchCombat(int64 nowMs)
{
    TakeCombatNotes(nowMs);
    Player* driver = Driver();
    if (!InWorldAndStill(driver))
        return;
    Map* map = driver->GetMap();

    // The casts of the enemies that the group fights (WatchEnemies keeps
    // only the living ones).
    std::vector<Bots::EnemyCastSeen> casts;
    for (auto const& pair : m_enemyHealth)
    {
        Creature* enemy = map->GetCreature(pair.first);
        if (!enemy)
            continue;
        RememberName(enemy);
        Bots::CastInfo const cast = BotAdapter::CastOf(enemy);
        casts.push_back({enemy->GetObjectGuid().GetRawValue(), enemy->IsAlive(),
            cast.interruptible ? cast.spell : Bots::NoSpell, cast.kind});
    }
    m_interrupts.Observe(casts, InterrupterFree(), nowMs);

    // The chase of each bot that is not the tank, and how far it is from the
    // living tank.
    Player* tank = nullptr;
    for (Player* member : Members())
    {
        Bots::BotRole role;
        if (IsLivingMember(member) && sBotMgr.BotRoleOf(member, role) && role == Bots::BotRole::Tank)
            tank = member;
    }
    std::vector<Bots::MemberChase> chases;
    for (Player* member : Members())
    {
        if (!tank || member == tank || member == driver || !IsLivingMember(member) || member->GetMap() != tank->GetMap())
            continue;
        Unit* target = BotAdapter::ChaseTargetOf(member);
        if (!target)
            continue;
        RememberName(member);
        RememberName(target);
        float const yards = member->GetDistance3dToCenter(tank);
        chases.push_back({member->GetObjectGuid().GetRawValue(), target->GetObjectGuid().GetRawValue(), yards});
    }
    std::size_t const known = m_chases.Chases().size();
    m_chases.Update(chases, nowMs);
    for (std::size_t i = known; i < m_chases.Chases().size(); ++i)
    {
        Bots::FarChase const& chase = m_chases.Chases()[i];
        std::string const text = NameOf(ObjectGuid(chase.member)) + " chases " + NameOf(ObjectGuid(chase.enemy))
            + " more than " + std::to_string(int(Bots::ChaseLimitYards)) + " yards from the tank";
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: %s", m_number, text.c_str());
        m_trace.Add(nowMs, text);
    }
}

void BotRun::TakeCombatNotes(int64 nowMs)
{
    for (RunCombatNote const& note : m_combatNotes.Drain())
    {
        Player* member = ObjectAccessor::FindPlayer(note.member);
        if (!member || !member->GetGroup() || member->GetGroup()->GetId() != m_groupId)
            continue;
        RememberName(member);
        if (Unit* enemy = member->IsInWorld() ? member->GetMap()->GetUnit(note.enemy) : nullptr)
            RememberName(enemy);
        Bots::Guid const who = note.member.GetRawValue(), enemy = note.enemy.GetRawValue();
        switch (note.kind)
        {
            case RunCombatNote::Kind::InterruptAttempt:
                m_interrupts.NoteAttempt(who, enemy, note.spell, nowMs);
                break;
            case RunCombatNote::Kind::InterruptMissed:
                m_interrupts.NoteMissed(who, enemy, nowMs);
                break;
            case RunCombatNote::Kind::CastStopped:
            {
                Bots::CastKind const kind = BotAdapter::CastKindOf(sSpellMgr.GetSpellEntry(note.spell));
                std::size_t const done = m_interrupts.Done().size();
                m_interrupts.NoteStopped(enemy, note.spell, kind, nowMs);
                if (m_interrupts.Done().size() > done)
                    m_trace.Add(nowMs, NameOf(ObjectGuid(m_interrupts.Done().back().member)) + " interrupts "
                        + NameOf(note.enemy) + " " + std::to_string(note.spell));
                break;
            }
            case RunCombatNote::Kind::FireNovaHit:
            {
                uint32 const fight = Bots::FightNumber(m_record.pulls);
                m_record.fireNovaHits.push_back({member->GetName(), RoleOf(member), fight, note.damage, nowMs});
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] Run %u: Fire Nova hits %s for %u", m_number,
                    member->GetName(), note.damage);
                m_trace.Add(nowMs, "Fire Nova hits " + std::string(member->GetName()) + " for " + std::to_string(note.damage));
                break;
            }
        }
    }
}

bool BotRun::InterrupterFree() const
{
    for (Player* member : Members())
    {
        if (!IsLivingMember(member))
            continue;
        for (auto const& known : member->GetSpellMap())
        {
            uint32 const first = sSpellMgr.GetFirstSpellInChain(known.first);
            if (!Bots::IsInterruptSpell(first) || !member->HasSpell(known.first))
                continue;
            SpellEntry const* entry = sSpellMgr.GetSpellEntry(known.first);
            // Shield Bash needs a shield.
            if (entry && member->IsSpellReady(entry) && member->HasItemFitToSpellReqirements(entry)
                && member->GetPower(Powers(entry->powerType)) >= entry->manaCost)
                return true;
        }
    }
    return false;
}

std::string BotRun::NameOf(ObjectGuid guid) const
{
    auto const found = m_unitNames.find(guid);
    return found != m_unitNames.end() ? found->second : guid.GetString();
}

void BotRun::RememberName(Unit const* unit)
{
    m_unitNames[unit->GetObjectGuid()] = unit->GetName();
}

Bots::BotRole BotRun::RoleOf(Player const* member) const
{
    Bots::BotRole role;
    return sBotMgr.BotRoleOf(member, role) ? role : Bots::BotRole::Ranged;
}

Bots::RunChatEvent* BotRun::ChatEventOf(uint64 id)
{
    auto const found = m_chatEvents.find(id);
    return found != m_chatEvents.end() ? &m_record.chatEvents[found->second] : nullptr;
}

void BotRun::NoteChatSent(SentChatEvent const& sent)
{
    if ((m_phase != Phase::Script && m_phase != Phase::Closing) || !m_groupId || sent.groupId != m_groupId)
        return;
    Player const* to = sent.toBot.IsEmpty() ? nullptr : ObjectAccessor::FindPlayer(sent.toBot);
    m_chatEvents[sent.id] = m_record.chatEvents.size();
    m_record.chatEvents.push_back({Bots::ChatEventName(sent.kind), to ? to->GetName() : "", RunMs(), false, {}});
}

void BotRun::NoteChatOutcome(Bots::ChatEventOutcome const& outcome)
{
    if (Bots::RunChatEvent* event = ChatEventOf(outcome.eventId))
        event->reached = outcome.reached;
}

void BotRun::NoteChatLine(BotLine const& line, Player const* bot)
{
    if (m_phase != Phase::Script && m_phase != Phase::Closing)
        return;
    if (line.extraPackWarn && m_groupId && bot->GetGroup() && bot->GetGroup()->GetId() == m_groupId &&
        !m_record.pulls.empty())
    {
        m_extraPacks.NoteWarn();
        RecordExtraPacks();
        m_trace.Add(RunMs(), std::string(bot->GetName()) + " warns about extra packs");
        return;
    }
    Bots::RunChatEvent* event = line.eventId ? ChatEventOf(line.eventId) : nullptr;
    if (!event)
        return;
    event->lines.push_back({bot->GetName(), RunMs(), line.chatType == CHAT_MSG_WHISPER ? "whisper" : "party"});
    m_trace.Add(RunMs(), std::string(bot->GetName()) + " says a line for " + event->event);
}
