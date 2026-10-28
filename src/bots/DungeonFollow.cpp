#include "DungeonFollow.h"

namespace Bots
{
    Guid HumanTankOf(std::vector<GroupMember> const& members, Guid leader)
    {
        Guid const tank = PickGroupTank(members, leader);
        return IsHumanTank(members, tank) ? tank : NoGuid;
    }

    bool FollowsTankThroughInstance(DungeonFollowFacts const& facts)
    {
        if (facts.scriptedRun || !facts.humanTank)
            return false;
        if (!facts.tank.inWorld || !facts.tank.alive || !facts.bot.inWorld || !facts.bot.alive)
            return false;
        if (facts.command == CommandState::Stay)
            return false;
        bool const samePlace =
            facts.tank.place.map == facts.bot.place.map && facts.tank.place.instance == facts.bot.place.instance;
        if (samePlace || (!facts.tank.inDungeon && !facts.bot.inDungeon))
            return false;
        return facts.msSinceLastTry >= DungeonFollowRetryMs;
    }
}
