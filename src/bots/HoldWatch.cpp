#include "HoldWatch.h"

namespace Bots
{
    std::vector<std::string> HoldWatch::Observe(GroupCoordinator const& group, std::int64_t nowMs)
    {
        std::vector<std::string> lines;
        PullState const pull = group.GetPull();
        if (pull.phase != PullPhase::Holding)
        {
            *this = HoldWatch();
            return lines;
        }
        if (!m_holding)
            lines.push_back("the tank engages its pull target");
        m_holding = true;
        if (pull.hasContact && !m_contact)
            lines.push_back("the pull target reaches the tank");
        m_contact = pull.hasContact;
        if (pull.backAtFightSpot && !m_back)
            lines.push_back("the tank is back at its fight spot");
        m_back = pull.backAtFightSpot;
        bool const held = group.TankHoldsThreat(nowMs);
        if (held && !m_held)
        {
            bool const byContact = m_back && m_contact && nowMs - pull.contactMs >= HoldContactMs;
            lines.push_back(std::string("the tank holds threat: ") + (byContact ? "melee contact" : "time limit"));
        }
        m_held = held;
        return lines;
    }
}
