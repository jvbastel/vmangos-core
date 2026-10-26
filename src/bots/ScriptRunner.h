#ifndef WOWAI_BOTS_SCRIPT_RUNNER_H
#define WOWAI_BOTS_SCRIPT_RUNNER_H

#include "RunScript.h"

#include <cstdint>
#include <string>
#include <vector>

// The generic runner of one scenario of a script file. It knows the order of
// the commands, the waits, and the limits. The game layer does each action
// and each check, and tells the runner the facts of the group:
//
//   RunStep step = runner.Update(facts, nowMs);
//   if (step.kind == RunStepKind::Do)
//       runner.Done(DoIt(step.command, detail), detail, nowMs);
namespace Bots
{
    // The limits of the waits, in milliseconds of the run clock.
    std::int64_t const TeleportLimitMs = 60000;
    std::int64_t const ArriveLimitMs = 60000;
    std::int64_t const FightStartLimitMs = 60000;
    // The group must be out of combat this long before a fight counts as
    // ended: a short gap between two enemies is no end.
    std::int64_t const CombatEndQuietMs = 3000;
    // At its end, a run holds its group at most this long for the chat of
    // the run: the HTTP timeout (5 s), the delays of the lines (about 7 s),
    // and the drop of a line that is more than 10 s late are each shorter.
    std::int64_t const RunEndChatWaitMs = 20000;

    // The run ended `heldMs` ago. True while it holds its group, so that the
    // line of its last boss_kill, wipe, or loot_roll still finds the bots in
    // the group (spec section 10): a chat line of the group is pending, and
    // the wait is shorter than RunEndChatWaitMs.
    inline bool RunEndWaitsForChat(std::int64_t heldMs, bool chatPending)
    {
        return chatPending && heldMs < RunEndChatWaitMs;
    }

    // The facts of the group at one update of the run.
    struct RunFacts
    {
        // A living group member is in combat.
        bool groupInCombat = false;
        // A group member is in a teleport, or not on the map of the driver.
        bool teleporting = false;
        // The test driver stands at the end of its last `move`.
        bool driverArrived = false;
    };

    enum class RunStepKind
    {
        Do,     // The game layer does `command` now, then calls Done.
        Wait,   // Nothing to do at this update.
        Passed, // Each command worked.
        Failed, // `reason` says why.
    };

    struct RunStep
    {
        RunStepKind kind;
        ScriptCommand command; // For Do.
        std::string reason;    // For Failed.
    };

    class ScriptRunner
    {
    public:
        explicit ScriptRunner(std::vector<ScriptCommand> commands);

        RunStep Update(RunFacts const& facts, std::int64_t nowMs);
        // The result of the last Do. `detail` says why it did not work.
        void Done(bool ok, std::string const& detail, std::int64_t nowMs);

        // "line <n>: <command>" of the command that runs or comes next, or
        // "the end".
        std::string Current() const;

    private:
        enum class State
        {
            Next,       // Take the next command.
            AwaitDone,  // A Do went out.
            Waiting,    // A wait command, or the arrival after a teleport.
            Passed,
            Failed,
        };

        RunStep Fail(std::string const& why);
        void Start(std::int64_t nowMs);

        std::vector<ScriptCommand> m_commands;
        std::size_t m_index = 0;
        State m_state = State::Next;
        std::int64_t m_since = 0;      // The start of the wait.
        bool m_fightSeen = false;      // wait combat_end: a fight started.
        std::int64_t m_quietSince = -1; // wait combat_end: out of combat since.
        std::string m_reason;
    };
}

#endif
