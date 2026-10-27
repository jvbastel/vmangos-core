#ifndef WOWAI_BOTS_SCRIPTED_MOVE_H
#define WOWAI_BOTS_SCRIPTED_MOVE_H

#include <cstdint>
#include <string>

// The scripted move of the test driver (jvbastel/WOWAI#99): a `move` of the
// script is in progress from its start until the driver arrives, or until
// its walk stops before it arrives. While it is in progress, the driver does
// not rest (BotView::IsScriptedMoveInProgress). The run never sends a
// stopped move again: `wait arrive` fails and names the stop reason. The
// game layer sees the arrival and the stop; this class keeps the state, the
// trace lines, and the count for the result file.
namespace Bots
{
    class ScriptedMove
    {
    public:
        // A new move of `driver` to the route point `point`. Its trace line.
        std::string Start(std::string const& driver, std::string const& point);
        bool InProgress() const { return m_inProgress; }
        // The driver arrived. The trace line, or empty when no move is in
        // progress.
        std::string Arrive();
        // The walk of the move stopped before it arrived, for `reason`, for
        // example "the rule drink". The trace line, or empty when no move is
        // in progress.
        std::string Stop(std::string const& reason);
        // A teleport or the end of the run ends the move, with no line.
        void End();

        // Why the last move stopped, or empty when it did not stop.
        std::string const& StopReason() const { return m_stopReason; }
        // The moves that stopped before they arrived.
        std::uint32_t StopCount() const { return m_stopped; }

    private:
        bool m_inProgress = false;
        std::string m_driver;
        std::string m_point;
        std::string m_stopReason;
        std::uint32_t m_stopped = 0;
    };
}

#endif
