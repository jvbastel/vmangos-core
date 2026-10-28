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
}
