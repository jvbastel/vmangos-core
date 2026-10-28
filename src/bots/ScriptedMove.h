#ifndef WOWAI_BOTS_SCRIPTED_MOVE_H
#define WOWAI_BOTS_SCRIPTED_MOVE_H

#include <cstdint>
#include <string>

// The scripted move of the test driver (jvbastel/WOWAI#99): a `move` of the
// script is in progress from its start until the driver arrives, or until
// its walk stops out of combat before it arrives. While it is in progress,
// the driver does not rest (BotView::IsScriptedMoveInProgress). The run
// never sends a move that stopped out of combat again: `wait arrive` fails
// and names the stop reason. A move that stops in a fight stays in progress
// and waits until the group was out of combat for CombatEndQuietMs; then it
// goes on (jvbastel/WOWAI#128). The game layer sees the arrival and the stop
// and sends the walk again; this class keeps the state, the trace lines, and
// the count for the result file.
namespace Bots
{
    class ScriptedMove
    {
    public:
        // A new move of `driver` to the route point `point`. Its trace line.
        std::string Start(std::string const& driver, std::string const& point);
        bool InProgress() const { return m_inProgress; }
        // The move stopped in a fight and waits for its end.
        bool Waits() const { return m_waits; }
        // The driver arrived. The trace line, or empty when no move is in
        // progress.
        std::string Arrive();
        // The walk of the move stopped before it arrived, for `reason`, for
        // example "the rule drink". In a fight (`inFight`), the move waits
        // for the end of the fight. The trace line, or empty when no move is
        // in progress or it waits already.
        std::string Stop(std::string const& reason, bool inFight = false);
        // At each update while the move waits: true once the group was out
        // of combat for CombatEndQuietMs. The game layer then sends the walk
        // again and calls GoOn.
        bool FightEnded(bool groupInCombat, std::int64_t nowMs);
        // The walk of a waiting move starts again. Its trace line, or empty
        // when the move does not wait.
        std::string GoOn();
        // A teleport or the end of the run ends the move, with no line.
        void End();

        // Why the last move stopped, or empty when it did not stop.
        std::string const& StopReason() const { return m_stopReason; }
        // The moves that stopped before they arrived, those that went on
        // after a fight included.
        std::uint32_t StopCount() const { return m_stopped; }

    private:
        bool m_inProgress = false;
        bool m_waits = false;
        std::int64_t m_quietSince = -1;
        std::string m_driver;
        std::string m_point;
        std::string m_stopReason;
        std::uint32_t m_stopped = 0;
    };
}

#endif
