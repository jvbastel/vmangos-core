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

    char const* StartReasonName(StartReason reason)
    {
        switch (reason)
        {
            case StartReason::Command:
                return "command";
            case StartReason::PlayerMoves:
                return "player moves";
            case StartReason::FightEnds:
                break;
        }
        return "fight ends";
    }

    std::uint32_t StartGate::OnMoveCommand(StartDelaySettings const& settings, RandomSource& random)
    {
        return Begin(StartReason::Command, settings, random);
    }

    void StartGate::Cancel()
    {
        m_waiting = false;
    }

    std::uint32_t StartGate::Begin(StartReason reason, StartDelaySettings const& settings, RandomSource& random)
    {
        m_waiting = true;
        m_reason = reason;
        m_delayMs = DrawStartDelay(settings, random);
        m_waitedMs = 0;
        return m_delayMs;
    }

    StartGate::Result StartGate::Update(std::uint32_t diffMs, StartFacts const& facts,
        StartDelaySettings const& settings, RandomSource& random)
    {
        bool const playerStarts = m_known && facts.playerMoves && !m_playerMoved;
        bool const fightEnds = m_known && m_wasInCombat && !facts.inCombat;
        m_known = true;
        m_playerMoved = facts.playerMoves;
        m_wasInCombat = facts.inCombat;

        Result result;
        if (m_waiting)
        {
            m_waitedMs += diffMs;
            if (m_waitedMs < m_delayMs)
                return result;
            m_waiting = false;
            result.step = Step::Start;
            result.reason = m_reason;
            return result;
        }

        if (facts.command != CommandState::Follow || facts.inCombat)
            return result;
        if (fightEnds)
            result.reason = StartReason::FightEnds;
        else if (playerStarts && facts.followsPlayer && facts.botStands)
            result.reason = StartReason::PlayerMoves;
        else
            return result;
        result.step = Step::Wait;
        result.delayMs = Begin(result.reason, settings, random);
        return result;
    }
}
