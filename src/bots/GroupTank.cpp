#include "GroupTank.h"

namespace Bots
{
    Guid PickGroupTank(std::vector<GroupMember> const& members, Guid leader)
    {
        // jvbastel/WOWAI#197: the role choice of a human comes first.
        for (GroupMember const& member : members)
            if (!member.isBot && member.choice == RoleChoice::Tank)
                return member.guid;
        for (GroupMember const& member : members)
            if (member.isBot && member.role == BotRole::Tank)
                return member.guid;
        for (GroupMember const& member : members)
            if (!member.isBot && member.guid == leader)
                return leader;
        return NoGuid;
    }

    bool IsPlayerTank(std::vector<GroupMember> const& members, Guid tank)
    {
        for (GroupMember const& member : members)
            if (!member.isBot && member.guid == tank)
                return tank != NoGuid;
        return false;
    }

    GroupTank GroupTankOf(std::vector<GroupMember> const& members, Guid leader)
    {
        Guid const tank = PickGroupTank(members, leader);
        return {tank, IsPlayerTank(members, tank)};
    }

    bool RefusesBotTank(std::vector<GroupMember> const& members)
    {
        for (GroupMember const& member : members)
            if (!member.isBot && member.choice == RoleChoice::Tank)
                return true;
        return false;
    }

    bool MustLeaveAsSecondTank(std::vector<GroupMember> const& members, Guid bot)
    {
        if (!RefusesBotTank(members))
            return false;
        for (GroupMember const& member : members)
            if (member.isBot && member.guid == bot)
                return member.role == BotRole::Tank;
        return false;
    }
}
