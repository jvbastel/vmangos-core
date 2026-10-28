#include "GroupTank.h"

namespace Bots
{
    Guid PickGroupTank(std::vector<GroupMember> const& members, Guid leader)
    {
        for (GroupMember const& member : members)
            if (member.isBot && member.role == BotRole::Tank)
                return member.guid;
        for (GroupMember const& member : members)
            if (!member.isBot && member.guid == leader)
                return leader;
        return NoGuid;
    }

    bool IsHumanTank(std::vector<GroupMember> const& members, Guid tank)
    {
        for (GroupMember const& member : members)
            if (!member.isBot && member.guid == tank)
                return tank != NoGuid;
        return false;
    }

    Guid NextGroupTank(std::vector<GroupMember> const& members, Guid leader, Guid current, bool currentIsBotTank)
    {
        Guid const pick = PickGroupTank(members, leader);
        // A bot tank out of `members` stays, also over a human leader: the
        // group stays a bot-tank group (merge review of jvbastel/WOWAI#189).
        if (currentIsBotTank && (pick == NoGuid || IsHumanTank(members, pick)))
            return current;
        return pick;
    }
}
