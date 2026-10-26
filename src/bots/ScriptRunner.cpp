#include "ScriptRunner.h"

namespace Bots
{
    namespace
    {
        bool IsWait(ScriptCommandKind kind)
        {
            return kind == ScriptCommandKind::WaitSeconds || kind == ScriptCommandKind::WaitArrive ||
                kind == ScriptCommandKind::WaitCombatEnd || kind == ScriptCommandKind::WaitRested;
        }

        std::string Seconds(std::int64_t ms)
        {
            return std::to_string(ms / 1000) + " s";
        }
    }

    ScriptRunner::ScriptRunner(std::vector<ScriptCommand> commands) : m_commands(std::move(commands))
    {
    }

    std::string ScriptRunner::Current() const
    {
        if (m_index >= m_commands.size())
            return "the end";
        ScriptCommand const& c = m_commands[m_index];
        return "line " + std::to_string(c.line) + ": " + c.source;
    }

    RunStep ScriptRunner::Fail(std::string const& why)
    {
        m_state = State::Failed;
        m_reason = Current() + ": " + why;
        return {RunStepKind::Failed, ScriptCommand(), m_reason};
    }

    void ScriptRunner::Start(std::int64_t nowMs)
    {
        m_since = nowMs;
        m_fightSeen = false;
        m_quietSince = -1;
    }

    RunStep ScriptRunner::Update(RunFacts const& facts, std::int64_t nowMs)
    {
        RunStep const wait = {RunStepKind::Wait, ScriptCommand(), ""};
        for (;;)
        {
            switch (m_state)
            {
                case State::Passed:
                    return {RunStepKind::Passed, ScriptCommand(), ""};
                case State::Failed:
                    return {RunStepKind::Failed, ScriptCommand(), m_reason};
                case State::AwaitDone:
                    return wait;
                case State::Next:
                {
                    if (m_index >= m_commands.size())
                    {
                        m_state = State::Passed;
                        continue;
                    }
                    ScriptCommand const& c = m_commands[m_index];
                    Start(nowMs);
                    if (IsWait(c.kind))
                    {
                        m_state = State::Waiting;
                        continue;
                    }
                    m_state = State::AwaitDone;
                    return {RunStepKind::Do, c, ""};
                }
                case State::Waiting:
                {
                    ScriptCommand const& c = m_commands[m_index];
                    std::int64_t const waited = nowMs - m_since;
                    bool done = false;
                    switch (c.kind)
                    {
                        case ScriptCommandKind::Teleport:
                            if (!facts.teleporting)
                                done = true;
                            else if (waited >= TeleportLimitMs)
                                return Fail("the group did not arrive in " + Seconds(TeleportLimitMs));
                            break;
                        case ScriptCommandKind::WaitSeconds:
                            done = waited >= std::int64_t(c.seconds) * 1000;
                            break;
                        case ScriptCommandKind::WaitArrive:
                            if (facts.driverArrived)
                                done = true;
                            else if (waited >= ArriveLimitMs)
                                return Fail("the test driver did not arrive in " + Seconds(ArriveLimitMs));
                            break;
                        case ScriptCommandKind::WaitRested:
                            if (!facts.groupResting)
                                done = true;
                            else if (waited >= std::int64_t(c.seconds) * 1000)
                                return Fail("the group still rests after " + Seconds(std::int64_t(c.seconds) * 1000));
                            break;
                        case ScriptCommandKind::WaitCombatEnd:
                        case ScriptCommandKind::Fight:
                            if (!m_fightSeen)
                            {
                                if (facts.groupInCombat)
                                {
                                    m_fightSeen = true;
                                    m_since = nowMs;
                                }
                                else if (waited >= FightStartLimitMs)
                                    return Fail("no fight started in " + Seconds(FightStartLimitMs));
                                break;
                            }
                            if (facts.groupInCombat)
                                m_quietSince = -1;
                            else if (m_quietSince < 0)
                                m_quietSince = nowMs;
                            if (m_quietSince >= 0 && nowMs - m_quietSince >= CombatEndQuietMs)
                                done = true;
                            else if (waited >= std::int64_t(c.seconds) * 1000)
                                return Fail("the fight did not end in " + Seconds(std::int64_t(c.seconds) * 1000));
                            break;
                        default:
                            done = true;
                            break;
                    }
                    if (!done)
                        return wait;
                    ++m_index;
                    m_state = State::Next;
                    continue;
                }
            }
        }
    }

    void ScriptRunner::Done(bool ok, std::string const& detail, std::int64_t nowMs)
    {
        if (m_state != State::AwaitDone)
            return;
        if (!ok)
        {
            Fail(detail);
            return;
        }
        ScriptCommandKind const kind = m_commands[m_index].kind;
        if (kind == ScriptCommandKind::Teleport || kind == ScriptCommandKind::Fight)
        {
            // The group arrives, or the fight starts and ends, over the next
            // updates.
            Start(nowMs);
            m_state = State::Waiting;
            return;
        }
        ++m_index;
        m_state = State::Next;
    }

    void ScriptRunner::NothingToDo()
    {
        if (m_state != State::AwaitDone)
            return;
        ++m_index;
        m_state = State::Next;
    }
}
