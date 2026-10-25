#include "StartDelay.h"

#include <algorithm>

namespace Bots
{
    SeededRandom::SeededRandom(std::uint32_t seed) : m_engine(seed)
    {
    }

    std::uint32_t SeededRandom::Between(std::uint32_t low, std::uint32_t high)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // By hand, not with uniform_int_distribution: its numbers differ
        // between standard libraries, so a seed would not repeat a run.
        std::uint64_t const span = std::uint64_t(high) - low + 1;
        return low + std::uint32_t(m_engine() % span);
    }

    void SeededRandom::Reseed(std::uint32_t seed)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_engine.seed(seed);
    }

    std::uint32_t DrawStartDelay(StartDelaySettings const& settings, RandomSource& random)
    {
        std::uint32_t const low = settings.minMs;
        std::uint32_t const high = std::max(settings.minMs, settings.maxMs);
        return std::min(high, std::max(low, random.Between(low, high)));
    }
}
