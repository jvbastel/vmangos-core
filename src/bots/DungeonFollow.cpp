#include "DungeonFollow.h"

namespace Bots
{
    bool FollowsTankThroughInstance(DungeonFollowFacts const& facts)
    {
        if (facts.scriptedRun || !facts.playerTank)
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
