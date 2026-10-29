#include "HoldWatch.h"

namespace Bots
{
    namespace
    {
        char const* HeldText(ThreatHeld by)
        {
            switch (by)
            {
                case ThreatHeld::OutsideAPull:
                    return "outside a pull";
                case ThreatHeld::MeleeContact:
                    return "melee contact";
                case ThreatHeld::TimeLimit:
                    return "time limit";
                case ThreatHeld::TopOfThreat:
                    return "top of threat";
                case ThreatHeld::No:
                    break;
            }
            return "no";
        }
    }

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
        ThreatHeld const by = group.TankHoldsThreatBy(nowMs);
        bool const held = by != ThreatHeld::No;
        if (held && !m_held)
            lines.push_back(std::string("the tank holds threat: ") + HeldText(by));
        m_held = held;
        return lines;
    }
}
