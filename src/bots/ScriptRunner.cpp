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

    ScriptRunner::ScriptRunner(std::vector<ScriptCommand> commands, Route route)
        : m_commands(std::move(commands)), m_route(std::move(route))
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
                    if (m_commands[m_index].kind == ScriptCommandKind::WalkBack)
                    {
                        ExpandWalkBack(facts.driverAt);
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
                            // A fight on the way stops the move until it
                            // ends (ScriptedMove.h): the limit counts from
                            // the end of the last fight.
                            else if (facts.groupInCombat)
                                m_since = nowMs;
                            else if (waited >= ArriveLimitMs)
                                return Fail("the test driver did not arrive in " + Seconds(ArriveLimitMs) +
                                    (facts.driverMoveStop.empty() ? "" : ", its move stopped: " + facts.driverMoveStop));
                            break;
                        case ScriptCommandKind::WaitRested:
                            if (!facts.groupResting && !facts.memberDead)
                                done = true;
                            else if (waited >= std::int64_t(c.seconds) * 1000)
                                return Fail((facts.memberDead ? "a member is still dead after " : "the group still rests after ") +
                                    Seconds(std::int64_t(c.seconds) * 1000));
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
                    if (c.kind == ScriptCommandKind::Fight && facts.fightPackAlive)
                    {
                        if (c.again >= std::uint32_t(RefightLimit))
                            return Fail(c.name + " still lives after " + std::to_string(RefightLimit + 1) + " fights");
                        FightAgain();
                    }
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

    std::string ScriptRunner::FightPack() const
    {
        if (m_state != State::Waiting || m_index >= m_commands.size() ||
            m_commands[m_index].kind != ScriptCommandKind::Fight)
            return "";
        return m_commands[m_index].name;
    }

    void ScriptRunner::FightAgain()
    {
        ScriptCommand const fight = m_commands[m_index];
        std::vector<ScriptCommand> again;
        auto add = [&](ScriptCommand step)
        {
            step.line = fight.line;
            again.push_back(step);
        };
        auto command = [&](ScriptCommandKind kind, std::string const& source)
        {
            ScriptCommand step;
            step.kind = kind;
            step.source = source;
            return step;
        };

        ScriptCommand rested = command(ScriptCommandKind::WaitRested, "wait rested");
        rested.seconds = DefaultRestedSeconds;
        // A wipe ends the fight before the release: the group lives again
        // at the dungeon entrance, rests, and follows the driver back. The
        // walk starts from where the driver stands after the rest.
        add(rested);
        // The walk back goes over the route points before the pack. With
        // none, the group goes back to the last `move` of the script.
        bool const walk = !WalkBackPoints(m_route, Position{0.0f, 0.0f, 0.0f}, fight.name).empty();
        std::size_t lastMove = m_index;
        for (std::size_t i = m_index; i-- > 0;)
            if (m_commands[i].kind == ScriptCommandKind::Move)
            {
                lastMove = i;
                break;
            }
        if (walk || lastMove < m_index)
        {
            ScriptCommand follow = command(ScriptCommandKind::Command, "command follow");
            follow.text = "follow";
            add(follow);
        }
        if (walk)
        {
            ScriptCommand back = command(ScriptCommandKind::WalkBack, "walk back to " + fight.name);
            back.name = fight.name;
            add(back);
        }
        else if (lastMove < m_index)
        {
            add(m_commands[lastMove]);
            add(command(ScriptCommandKind::WaitArrive, "wait arrive"));
        }
        // A member that died on the way back.
        add(rested);
        // The group held its place for the pull: it does so again.
        if (m_index > 0 && m_commands[m_index - 1].kind == ScriptCommandKind::Command &&
            m_commands[m_index - 1].text == "stay")
            add(m_commands[m_index - 1]);
        ScriptCommand next = fight;
        ++next.again;
        add(next);
        m_commands.insert(m_commands.begin() + m_index + 1, again.begin(), again.end());
    }

    void ScriptRunner::ExpandWalkBack(Position const& from)
    {
        ScriptCommand const back = m_commands[m_index];
        std::vector<ScriptCommand> walk;
        for (std::string const& point : WalkBackPoints(m_route, from, back.name))
        {
            ScriptCommand move = back;
            move.kind = ScriptCommandKind::Move;
            move.name = point;
            move.source = "move " + point;
            walk.push_back(move);
            ScriptCommand arrive = back;
            arrive.kind = ScriptCommandKind::WaitArrive;
            arrive.name.clear();
            arrive.source = "wait arrive";
            walk.push_back(arrive);
        }
        m_commands.erase(m_commands.begin() + m_index);
        m_commands.insert(m_commands.begin() + m_index, walk.begin(), walk.end());
    }

    void ScriptRunner::NothingToDo()
    {
        if (m_state != State::AwaitDone)
            return;
        ++m_index;
        m_state = State::Next;
    }
}
