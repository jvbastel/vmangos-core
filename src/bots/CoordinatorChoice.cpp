#include "CoordinatorChoice.h"

namespace Bots
{
    CoordinatorChoice ChooseCoordinator(bool inGroup, MapPlace bot, LeaderPlace leader)
    {
        if (!inGroup)
            return CoordinatorChoice::Own;
        // Without the leader's map the bot keeps the group's state.
        if (!leader.known)
            return CoordinatorChoice::Group;
        // The bots of one group can sit on two map threads: a bot away from
        // the leader ignores the group until it arrives (spec section 6). It
        // shares the state of its place with the bots of its group there.
        bool const samePlace = bot.map == leader.place.map && bot.instance == leader.place.instance;
        return samePlace ? CoordinatorChoice::Group : CoordinatorChoice::Place;
    }
}
