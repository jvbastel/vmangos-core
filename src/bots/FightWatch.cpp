#include "FightWatch.h"

#include <cstdio>

namespace Bots
{
    void FightWatch::Start(std::uint32_t number, std::string const& pack, bool again, std::int64_t ms,
        std::vector<std::pair<std::string, Guid>> const& members)
    {
        m_running = true;
        m_inCombat = false;
        m_fight = FightSummary();
        m_fight.number = number;
        m_fight.pack = pack;
        m_fight.again = again;
        m_fight.startMs = ms;
        m_order.clear();
        m_members.clear();
        m_enemies.clear();
        for (auto const& member : members)
        {
            m_order.push_back(member.first);
            m_members[member.first].guid = member.second;
        }
    }

    bool FightWatch::Observe(std::int64_t ms, bool groupInCombat, std::set<Guid> const& enemies,
        std::vector<FightMemberSeen> const& members)
    {
        if (!m_running)
            return false;
        m_enemies.insert(enemies.begin(), enemies.end());
        for (FightMemberSeen const& seen : members)
        {
            auto found = m_members.find(seen.name);
            if (found == m_members.end())
                continue;
            Watched& watched = found->second;
            if (seen.victim != NoGuid && seen.victim != watched.victim)
                watched.actions.push_back({ms, seen.victim, false, CastKind::None});
            watched.victim = seen.victim;
            if (seen.spell != NoSpell && (seen.spell != watched.spell || seen.spellTarget != watched.spellTarget))
                watched.actions.push_back({ms, seen.spellTarget, true, seen.kind});
            watched.spell = seen.spell;
            watched.spellTarget = seen.spellTarget;
        }
        bool const over = m_inCombat && !groupInCombat;
        m_inCombat = groupInCombat;
        return over;
    }

    FightSummary FightWatch::End(std::int64_t ms)
    {
        std::set<Guid> group;
        for (auto const& member : m_members)
            group.insert(member.second.guid);
        FightSummary summary = m_fight;
        summary.endMs = ms;
        for (std::string const& name : m_order)
        {
            FightMember member;
            member.name = name;
            for (Action const& action : m_members[name].actions)
            {
                // A mob of the fight, or a heal at a member.
                bool const part = m_enemies.count(action.target) ||
                    (action.cast && action.kind == CastKind::Heal && group.count(action.target));
                if (!part)
                    continue;
                if (!member.joined)
                {
                    member.joined = true;
                    member.firstActionMs = action.ms - m_fight.startMs;
                }
                if (action.cast)
                    ++member.casts;
            }
            summary.members.push_back(member);
        }
        m_running = false;
        return summary;
    }

    std::string FightText(FightSummary const& summary)
    {
        std::string text = "fight " + std::to_string(summary.number) + " " +
            (summary.pack.empty() ? std::string("pull") : summary.pack) + (summary.again ? " again" : "") + ":";
        char const* separator = " ";
        for (FightMember const& member : summary.members)
        {
            text += separator + member.name;
            separator = ", ";
            if (!member.joined)
            {
                text += " did not join";
                continue;
            }
            char part[64];
            std::snprintf(part, sizeof(part), " %.1f s %u cast%s", double(member.firstActionMs) / 1000.0,
                unsigned(member.casts), member.casts == 1 ? "" : "s");
            text += part;
        }
        return text;
    }
}
