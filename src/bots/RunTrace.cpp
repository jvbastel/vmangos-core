#include "RunTrace.h"

#include "BotGeometry.h"

#include <cstdio>

namespace Bots
{
    std::string FleeLine(std::string const& enemy, Position const& at)
    {
        char text[64];
        std::snprintf(text, sizeof(text), " flees at %.2f %.2f %.2f", at.x, at.y, at.z);
        return enemy + text;
    }

    namespace
    {
        // A move shorter than this is no new position.
        float const MoveYards = 1.0f;
        std::int64_t const PositionEveryMs = 1000;

        std::string PositionLine(MemberState const& member)
        {
            char text[96];
            std::snprintf(text, sizeof(text), " at %.2f %.2f %.2f", member.position.x, member.position.y, member.position.z);
            return member.name + text;
        }

        std::string TargetLine(MemberState const& member)
        {
            return member.name + " targets " + (member.target.empty() ? std::string("nothing") : member.target);
        }

        std::string CastLine(MemberState const& member)
        {
            return member.name + " casts " + std::to_string(member.spell) +
                (member.spellName.empty() ? "" : " " + member.spellName);
        }

        std::string EventLine(std::pair<std::int64_t, std::string> const& event)
        {
            return std::to_string(event.first / 1000) + "." + std::to_string(event.first % 1000 / 100) + " " +
                event.second;
        }
    }

    RunTrace::RunTrace(std::int64_t keepMs) : m_keepMs(keepMs)
    {
    }

    void RunTrace::Add(std::int64_t ms, std::string const& line)
    {
        m_events.emplace_back(ms, line);
        m_new.emplace_back(ms, line);
        while (m_keepMs != TraceKeepWholeRun && !m_events.empty() && m_events.front().first < ms - m_keepMs)
            m_events.pop_front();
    }

    void RunTrace::Observe(std::int64_t ms, MemberState const& member)
    {
        auto found = m_seen.find(member.name);
        if (found == m_seen.end())
        {
            m_seen[member.name] = {ms, member.position, member.target, member.spell};
            Add(ms, PositionLine(member));
            if (!member.target.empty())
                Add(ms, TargetLine(member));
            if (member.spell)
                Add(ms, CastLine(member));
            return;
        }
        Seen& seen = found->second;
        if (ms - seen.positionMs >= PositionEveryMs && Distance3d(seen.position, member.position) >= MoveYards)
        {
            seen.positionMs = ms;
            seen.position = member.position;
            Add(ms, PositionLine(member));
        }
        if (member.target != seen.target)
        {
            seen.target = member.target;
            Add(ms, TargetLine(member));
        }
        if (member.spell != seen.spell)
        {
            seen.spell = member.spell;
            if (member.spell)
                Add(ms, CastLine(member));
        }
    }

    std::vector<std::string> RunTrace::Lines() const
    {
        std::vector<std::string> lines;
        for (auto const& event : m_events)
            lines.push_back(EventLine(event));
        return lines;
    }

    std::vector<std::string> RunTrace::TakeNew()
    {
        std::vector<std::string> lines;
        for (auto const& event : m_new)
            lines.push_back(EventLine(event));
        m_new.clear();
        return lines;
    }

    std::string RunTrace::Text() const
    {
        std::string text;
        for (std::string const& line : Lines())
            text += line + "\n";
        return text;
    }
}
