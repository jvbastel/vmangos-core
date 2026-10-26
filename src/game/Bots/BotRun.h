/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_BOTRUN_H
#define WOWAI_GAME_BOTRUN_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Route.h"
#include "RunScript.h"
#include "ScriptRunner.h"
#include "ThreatWatch.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

class Player;

// The script controller of the scripted runs (spec section 10). It runs one
// scenario of a script file at a time: first a full reset of the test driver
// and the four bots, then the commands of the scenario through the generic
// ScriptRunner. The test driver is a normal bot in the player's seat: its
// command lines go through BotHooks::OnPlayerChat with the driver as the
// sender. It runs on the world thread, in BotMgr::Update.
class BotRun
{
    public:
        // Starts a run of `<DataDir>/bottests/<script>.txt`. False with a
        // short line for the GM.
        bool Start(std::string const& dataDir, std::string const& scriptName, std::string const& scenario, std::string& error);
        // Ends a running run as failed.
        bool Stop(std::string& error);
        void Update();
        // A line for the GM: the running step, or the result of the last run.
        std::string Status() const;

        // A boss of the route died for the group of the run.
        void NoteBossKill(uint32 groupId, uint32 entry);

    private:
        enum class Phase
        {
            Idle,
            LoginFirst,  // Each member logs in, wherever it logged out.
            GoOutside,   // Each member goes out of the dungeon.
            Logout,      // A new login gives each bot a fresh AI.
            Login,
            Script,      // The ScriptRunner runs the scenario.
            Passed,
            Failed,
        };

        // The run is over: passed or failed.
        bool IsOver() const { return m_phase == Phase::Passed || m_phase == Phase::Failed; }

        // Ends the run as failed, and leaves the run group.
        void Fail(std::string const& reason);
        // The end of a run, passed or failed: the members leave the run
        // group, so that a later run, check, or player invite starts from
        // a clean state (jvbastel/WOWAI#82). A bot out of its group logs
        // out (BotMgr::UpdateGroupOf).
        void LeaveGroup();
        void SetPhase(Phase phase);
        bool PhaseTimedOut(std::string const& what);
        std::vector<Player*> Members() const;
        Player* Driver() const;
        bool AllOnline() const;
        bool AllOffline() const;
        void Prepare();
        bool FormGroup(std::string& error);
        void ResetMember(Player* member);
        Bots::RunFacts Facts() const;
        // Tells the threat watch which enemy hits which member who is not
        // the tank, and logs each new threat loss. Tells the lost mob watch
        // which enemy hits which member and which enemies the group attacks,
        // and fails the run at a lost mob.
        void WatchThreat(int64 nowMs);
        // "<enemy> on <member>", for the log and the result.
        std::string HitName(Bots::Guid enemy, Bots::Guid member) const;
        // ", threat losses <n> (<enemy> on <member> <s> s; ...), lost mobs
        // <m> (...)": both counts, for the result line (spec section 10).
        std::string WatchText() const;
        // Does one command of the scenario. False with the reason in `detail`.
        bool Do(Bots::ScriptCommand const& command, std::string& detail);
        bool Teleport(Bots::RoutePoint const& point, std::string& detail);
        void TeleportRest();
        bool Move(Bots::RoutePoint const& point, std::string& detail);
        bool Say(uint32 chatType, Player* receiver, std::string const& text);
        bool Target(Bots::ScriptCommand const& command, std::string& detail);
        bool Clear(Bots::ScriptCommand const& command, std::string& detail);
        void Refill();
        bool ExpectBossDead(uint32 entry, std::string& detail);
        bool ExpectPackDead(std::string const& pack, std::string& detail);
        bool ExpectAlive(std::string& detail);
        // The revive rule of the scripted runs (jvbastel/WOWAI#72): the run
        // cuts the ghost run short. Out of combat, a released member
        // revives at once at the living member nearest to its corpse. In a
        // wipe, every member revives at the dungeon entrance.
        void ReviveTheDead();
        void ReviveAt(Player* member, uint32 mapId, float x, float y, float z, float o);
        // A random spot of a random place of the point. The run logs it.
        Bots::RoutePlace PickPlace(Bots::RoutePoint const& point, std::string const& who, float& x, float& y);

        Phase m_phase = Phase::Idle;
        uint32 m_number = 0; // The runs since the start of mangosd.
        Bots::RunScript m_script;
        Bots::Route m_route;
        std::string m_scenario;
        std::unique_ptr<Bots::ScriptRunner> m_runner;
        int64 m_startMs = 0;
        int64 m_phaseMs = 0;
        std::string m_result;
        uint32 m_groupId = 0; // The run group, from FormGroup to the end of the run.
        std::set<uint32> m_bossKills;
        Bots::ThreatWatch m_threat;
        std::vector<std::string> m_threatNames; // "<enemy> on <member>" of each loss.
        Bots::LostMobWatch m_lostMobs;
        std::vector<std::string> m_lostMobNames; // "<enemy> on <member>" of each lost mob.
        // A teleport of the bots that waits for the driver on the new map.
        bool m_teleportRest = false;
        Bots::RoutePoint m_teleportPoint;
        // The end of the last `move` of the driver.
        bool m_moving = false;
        float m_moveX = 0.0f, m_moveY = 0.0f, m_moveRadius = 0.0f;
        // The start of the wipe, or 0 while a member lives.
        int64 m_wipeMs = 0;
};

#endif
