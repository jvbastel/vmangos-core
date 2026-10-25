#ifndef WOWAI_BOTS_START_DELAY_H
#define WOWAI_BOTS_START_DELAY_H

#include "CommandState.h"

#include <cstdint>
#include <mutex>
#include <random>

// The start delay: bots of a group do not all start to move at the same
// moment. Before a bot starts to move, it waits its own short random delay.
// The delay is new each time, and it comes from the random source of the
// run, so a scripted run can record it with its other random choices.
namespace Bots
{
    // A source of random numbers. The game layer gives the library one, so
    // the tests give fixed numbers and a scripted run can record each draw.
    class RandomSource
    {
    public:
        virtual ~RandomSource() = default;
        // A number from `low` to `high`, both included. `low` <= `high`.
        virtual std::uint32_t Between(std::uint32_t low, std::uint32_t high) = 0;
    };

    // A random source from a seed: the same seed gives the same numbers.
    // Safe to use from more than one thread.
    class SeededRandom : public RandomSource
    {
    public:
        explicit SeededRandom(std::uint32_t seed);

        std::uint32_t Between(std::uint32_t low, std::uint32_t high) override;
        // Starts the numbers again from a new seed.
        void Reseed(std::uint32_t seed);

    private:
        std::mutex m_mutex;
        std::mt19937 m_engine;
    };

    // The limits of the delay, from the settings `Bots.StartDelayMs.Min` and
    // `Bots.StartDelayMs.Max`.
    struct StartDelaySettings
    {
        std::uint32_t minMs = 300;
        std::uint32_t maxMs = 1500;
    };

    // One delay from `minMs` to `maxMs`, both included. A maximum below the
    // minimum counts as the minimum. A number of the source outside the
    // limits is clamped to them.
    std::uint32_t DrawStartDelay(StartDelaySettings const& settings, RandomSource& random);

    // Why a bot waits before it moves.
    enum class StartReason
    {
        Command,     // A `follow` or `come` command.
        PlayerMoves, // The player that the bot follows starts to move.
        FightEnds,   // The bot goes back to follow after a fight.
    };

    char const* StartReasonName(StartReason reason);

    // What the game layer knows about the bot at one update.
    struct StartFacts
    {
        CommandState command;
        bool inCombat;
        // The bot follows the player of its command now.
        bool followsPlayer;
        // The bot is at its place: it does not walk.
        bool botStands;
        // The player of the command moves.
        bool playerMoves;
    };

    // The start delay of one bot. The game layer calls Update at each update
    // of the bot, and OnMoveCommand or Cancel at each bot command that moves
    // or stops the bot.
    //
    // A wait begins:
    // - at a `follow` or `come` command (OnMoveCommand),
    // - when the player that a following bot follows starts to move, if the
    //   bot stands at its place (not when it still walks to it),
    // - when the fight of a following bot ends.
    // Each wait draws a new delay. A new wait during a wait replaces it.
    // `stop` and `stay` do not wait: Cancel ends a wait with no start.
    class StartGate
    {
    public:
        enum class Step
        {
            None,
            // A wait began. For PlayerMoves the game layer stops the bot,
            // which otherwise follows at once.
            Wait,
            // The wait ended: the bot starts to move now.
            Start,
        };

        struct Result
        {
            Step step = Step::None;
            StartReason reason = StartReason::Command;
            std::uint32_t delayMs = 0; // The delay of a new wait (Step::Wait).
        };

        // A `follow` or `come` command: the delay of the new wait.
        std::uint32_t OnMoveCommand(StartDelaySettings const& settings, RandomSource& random);
        // `stop` or `stay`: no wait, and no start.
        void Cancel();
        bool Waiting() const { return m_waiting; }

        // Adds the time since the last update. The first update after
        // OnMoveCommand adds nothing: that time passed before the command.
        Result Update(std::uint32_t diffMs, StartFacts const& facts, StartDelaySettings const& settings,
            RandomSource& random);

    private:
        std::uint32_t Begin(StartReason reason, StartDelaySettings const& settings, RandomSource& random);

        bool m_waiting = false;
        StartReason m_reason = StartReason::Command;
        std::uint32_t m_delayMs = 0;
        std::uint32_t m_waitedMs = 0;
        // The wait began outside Update: its next update does not count.
        bool m_skipNextDiff = false;
        // The facts of the last update, to see a start of the player and the
        // end of a fight. Unknown before the first update.
        bool m_known = false;
        bool m_playerMoved = false;
        bool m_wasInCombat = false;
    };
}

#endif
