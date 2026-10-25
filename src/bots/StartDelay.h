#ifndef WOWAI_BOTS_START_DELAY_H
#define WOWAI_BOTS_START_DELAY_H

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
}

#endif
