/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_BOTRUN_H
#define WOWAI_GAME_BOTRUN_H

#include "BotChat.h"
#include "ChaseWatch.h"
#include "Common.h"
#include "ExtraPackWatch.h"
#include "FightWatch.h"
#include "GroupCoordinator.h"
#include "HoldWatch.h"
#include "Inbox.h"
#include "InterruptWatch.h"
#include "ObjectGuid.h"
#include "Route.h"
#include "RouteQuests.h"
#include "RunJudge.h"
#include "RunResult.h"
#include "RunScript.h"
#include "RunTrace.h"
#include "ScriptRunner.h"
#include "ScriptedMove.h"
#include "ThreatWatch.h"
#include "WipeRevive.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

class Creature;
class Player;
class Unit;
struct BotLine;

// A fact of a scripted run that a bot sees on its map thread (spec section
// 10, Results): an interrupt spell that it cast at an enemy cast, an
// interrupt of its own that missed, an enemy cast that stopped before it went
// off, or a Fire Nova of Taragaman that hit it.
struct RunCombatNote
{
    enum class Kind
    {
        InterruptAttempt,
        InterruptMissed,
        CastStopped,
        FireNovaHit,
    };

    Kind kind;
    ObjectGuid member; // The bot that saw it.
    ObjectGuid enemy;
    uint32 spell;  // The enemy cast of an interrupt or a stop, or Fire Nova.
    uint32 damage; // Of a Fire Nova.
};

// The script controller of the scripted runs (spec section 10). It runs one
// scenario of a script file at a time: first a full reset of the test driver
// and the four bots, then the commands of the scenario through the generic
// ScriptRunner. The test driver is a normal bot in the player's seat: its
// command lines go through BotHooks::OnPlayerChat with the driver as the
// sender. It runs on the world thread, in BotMgr::Update.
//
// A scenario with a `route` line is a full run: it passes when the last boss
// of the route dies. Each run ends at the rules of Bots::RunJudge, and
// writes one result file (Bots::RunResult) to Bots.Run.ResultDir; a failed
// or stopped run also writes the trace of its last 3 minutes there.
class BotRun
{
    public:
        // Starts a run of `<DataDir>/bottests/<script>.txt`. False with a
        // short line for the GM.
        bool Start(std::string const& dataDir, std::string const& scriptName, std::string const& scenario, std::string& error);
        // Ends a running run as aborted.
        bool Stop(std::string& error);
        void Update();
        // A line for the GM: the running step, or the result of the last run.
        std::string Status() const;

        // A boss of the route died for the group of the run.
        void NoteBossKill(uint32 groupId, uint32 entry);

        // From any thread: a combat fact of a member of the run. The run
        // takes the notes at its next update.
        void QueueCombatNote(RunCombatNote const& note) { m_combatNotes.Push(note); }
        // Bot chat, on the world thread (BotMgr::Update): an event that bot
        // chat sent, its outcome, and a line of the chat service that bot
        // `bot` said in game. The run keeps those of its group in the result.
        void NoteChatSent(SentChatEvent const& sent);
        void NoteChatOutcome(Bots::ChatEventOutcome const& outcome);
        void NoteChatLine(BotLine const& line, Player const* bot);

        // From any thread: a scripted move of the test driver is in progress
        // (BotView::IsScriptedMoveInProgress, jvbastel/WOWAI#99).
        bool IsScriptedMoveInProgress() const { return m_moveInProgress; }
        // From the map thread of the test driver: rule `rule` of its bot AI
        // took it off its scripted move. The next update names the rule as
        // the stop reason.
        void NoteDriverMoveRule(std::string const& rule);

    private:
        enum class Phase
        {
            Idle,
            LoginFirst,  // Each member logs in, wherever it logged out.
            GoOutside,   // Each member goes out of the dungeon.
            Logout,      // A new login gives each bot a fresh AI.
            Login,
            Script,      // The ScriptRunner runs the scenario.
            Closing,     // The run has its result. The group waits for the chat of the run.
            Passed,
            Failed,
        };

        // The run is over: passed or failed.
        bool IsOver() const { return m_phase == Phase::Passed || m_phase == Phase::Failed; }

        // Ends the run as failed: Finish(false, reason).
        void Fail(std::string const& reason);
        // The one end of a run, passed or failed. In this order:
        // 1. The result line, for the GM and the log.
        // 2. The result file, and the trace of the last 3 minutes of a run
        //    that did not pass (spec section 10), while the group still is.
        // 3. The phase Closing: the group stays until the chat of the run is
        //    said or dropped (Bots::RunEndWaitsForChat), so that the last
        //    boss_kill, wipe, or loot_roll gets its line. Then EndClosing.
        void Finish(bool passed, std::string const& reason);
        // In Closing: ends the wait now (EndRunGroup) and sets Passed or
        // Failed. A run start or a run stop cuts the wait short, so the wait
        // never blocks them; `cutBy` names the cause for the log (nullptr
        // when the wait ends by itself). False when the run is not in Closing.
        bool EndClosing(char const* cutBy);
        // After Closing: the test driver is a normal bot again, and the
        // members leave the run group, so that a later run, check, or
        // player invite starts from a clean state (jvbastel/WOWAI#82). A bot
        // out of its group logs out (BotMgr::UpdateGroupOf).
        void EndRunGroup();
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
        // The names of the enemy and the member, for the log and the result.
        std::pair<std::string, std::string> HitNames(Bots::Guid enemy, Bots::Guid member) const;
        // The facts of the run for the judge, the result, and the trace: the
        // deaths, the kills, the damage dealt, the moves of the test driver,
        // the quest visits, and what each member does. Then the verdict of
        // the judge.
        void WatchRun(int64 nowMs);
        // The test driver stands at the end of its last `move`.
        bool DriverArrived(Player const* driver) const;
        // The last `move` is still the motion of the driver: its point is
        // on top of the motion master.
        bool OnScriptedMove(Player* driver) const;
        // The walk of the driver to the point of its last `move`.
        void SendMove(Player* driver);
        // The scripted move: its trace lines, its stop, its go-on after a
        // fight, and its arrival (jvbastel/WOWAI#99, jvbastel/WOWAI#128).
        void WatchMove(Player* driver, int64 nowMs);
        // A teleport or the end of the run ends the move.
        void EndMove();
        // The enemies that the group fights: a kill or damage dealt is
        // progress, and a kill counts for the quests.
        void WatchEnemies(int64 nowMs);
        void TraceMembers(int64 nowMs);
        // Who takes part in the fight of the last pull (Bots::FightWatch):
        // what each member attacks and casts at, and the enemies of the
        // group. At the end of the fight, EndFight.
        void WatchFight(int64 nowMs);
        // The fight summary into the result, the trace, and the log.
        void EndFight(int64 nowMs);
        // The packs of the route that join the fight of the last pull
        // (Bots::ExtraPackWatch), into the result, the trace, and the log.
        void WatchExtraPacks(int64 nowMs);
        // At a pull: the pulls the tank could have made (Bots::
        // FindPullChoices), with each other pack of the route round it, on
        // the real packs and from the tank's cleared way at the pull
        // (jvbastel/WOWAI#123). A pack that joins is marked from them.
        void MarkPullPacks(Unit const* target, std::string const& pullPack);
        // What brought pack `pack` when it joined: a flee or a patrol.
        Bots::ExtraPackCause CauseOf(std::string const& pack) const;
        // The held pull (Bots::HoldWatch) into the trace and the log
        // (jvbastel/WOWAI#127).
        void WatchHold(int64 nowMs);
        // The extra packs of the watch into the last pull of the result.
        void RecordExtraPacks();
        // The new lines of the trace, appended to the live trace file
        // (LiveTraceFile in Bots.Run.ResultDir), so that the watch script
        // (wowai/test-realm/watch.py) follows the run as it goes.
        void WriteLiveTrace();
        // The interrupts, the chases beyond the limit, and the Fire Nova
        // hits (spec section 10, Results): the combat notes of the members,
        // the casts of the enemies that the group fights, and the chase of
        // each bot.
        void WatchCombat(int64 nowMs);
        void TakeCombatNotes(int64 nowMs);
        // A living member knows an interrupt spell that is ready, and has
        // the power for it.
        bool InterrupterFree() const;
        // The name of a unit that the run saw, for the result.
        std::string NameOf(ObjectGuid guid) const;
        void RememberName(Unit const* unit);
        // The role of a member for the result: the test driver is ranged.
        Bots::BotRole RoleOf(Player const* member) const;
        // The chat event of the run with the id, or null.
        Bots::RunChatEvent* ChatEventOf(uint64 id);
        // A living bot of the group eats or drinks.
        bool GroupResting() const;
        // The coordinator of the run group that bot `member` uses where it
        // is now, with the test driver as the leader (BotMgr::GetCoordinator).
        std::shared_ptr<Bots::GroupCoordinator> CoordinatorOf(Player const* member) const;
        // A living mob of the pack near the test driver, or null.
        Creature* LivingMobOf(std::string const& pack) const;
        bool Fight(Bots::ScriptCommand const& command, std::string& detail);
        // A pull of the result: a `fight`, or a `command pull`.
        void RecordPull(Bots::ScriptCommand const& command);
        // Writes the result file, and the trace when the run did not pass.
        void WriteResultFiles();
        // Writes the result file only: at the end of Closing again, with the
        // chat lines that came while the group waited for them.
        bool WriteResultJson() const;
        // The threat losses and the lost mobs of the watches, by name, into
        // the result.
        void RecordHits();
        // The interrupts and the chases of the watches, by name, into the
        // result.
        void RecordCombat();
        int64 RunMs() const;
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
        // wipe, every member in the world revives at the dungeon entrance
        // (WipeRevive.h).
        void ReviveTheDead();
        void ReviveAt(Player* member, uint32 mapId, float x, float y, float z, float o);
        void NoteWipe();
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
        bool m_passed = false; // The result of Finish, for the end of Closing.
        uint32 m_groupId = 0; // The run group, from FormGroup to the end of the run.
        std::set<uint32> m_bossKills;
        Bots::ThreatWatch m_threat;
        std::vector<std::pair<std::string, std::string>> m_threatNames; // The enemy and the member of each loss.
        Bots::LostMobWatch m_lostMobs;
        std::vector<std::pair<std::string, std::string>> m_lostMobNames; // The enemy and the member of each lost mob.
        bool m_aborted = false; // A GM stopped the run.
        std::unique_ptr<Bots::RunJudge> m_judge;
        Bots::InterruptWatch m_interrupts;
        Bots::ChaseWatch m_chases;
        Bots::Inbox<RunCombatNote> m_combatNotes;
        std::map<ObjectGuid, std::string> m_unitNames;
        std::map<uint64, std::size_t> m_chatEvents; // By event id: the index in m_record.chatEvents.
        Bots::RunTrace m_trace;
        Bots::FightWatch m_fight;
        Bots::ExtraPackWatch m_extraPacks;
        Bots::HoldWatch m_hold;
        Bots::RunResult m_record; // The result file.
        std::unique_ptr<Bots::QuestProgress> m_quests;
        // `release`: in a wipe, the test driver releases this long after the
        // last member died, or never.
        int64 m_releaseMs = 0;
        bool m_releaseNever = false;
        // The members that lived at the last update, by name.
        std::map<std::string, bool> m_alive;
        // The health of each enemy that the group fights.
        std::map<ObjectGuid, uint32> m_enemyHealth;
        // The enemies of the fight that flee now: each start of a flee is a
        // trace event (Bots::FleeLine, jvbastel/WOWAI#121).
        std::set<ObjectGuid> m_fleeing;
        // The enemies that fled in the last pull, with their trace text, and
        // the pulls the tank could have made (MarkPullPacks), and what
        // brought each extra pack when it joined (CauseOf).
        std::map<ObjectGuid, std::string> m_pullFled;
        Bots::PullChoices m_pullChoices;
        std::map<std::string, Bots::ExtraPackCause> m_joinCause;
        // MarkPullPacks saw a living tank and the target at the last pull.
        bool m_pullJudged = false;
        // Where the test driver stood at its last move along the route.
        bool m_driverSeen = false;
        float m_driverX = 0.0f, m_driverY = 0.0f, m_driverZ = 0.0f;
        // A teleport of the bots that waits for the driver on the new map.
        bool m_teleportRest = false;
        Bots::RoutePoint m_teleportPoint;
        // The end of the last `move` of the driver.
        bool m_moving = false;
        float m_moveX = 0.0f, m_moveY = 0.0f, m_moveZ = 0.0f, m_moveRadius = 0.0f;
        std::string m_movePoint;
        bool m_visited = false; // The quests know that the driver reached m_movePoint.
        Bots::ScriptedMove m_move;
        std::atomic<bool> m_moveInProgress{false}; // m_move.InProgress(), for the map threads.
        std::mutex m_moveRuleMutex;
        std::string m_moveRule; // NoteDriverMoveRule, empty when no rule took the driver off its move.
        Bots::WipeWatch m_wipeWatch;
};

#endif
