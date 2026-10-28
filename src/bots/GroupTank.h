#ifndef WOWAI_BOTS_GROUP_TANK_H
#define WOWAI_BOTS_GROUP_TANK_H

#include "BotRecipe.h"
#include "BotTypes.h"

#include <vector>

// Review of spec #131, story 10: who the group's tank is
// (GroupCoordinator::GetTank).
namespace Bots
{
    // A member of the group, as the adapter knows it.
    struct GroupMember
    {
        Guid guid;
        // A bot of the bot system, with role `role`. A human player has no
        // role: the game keeps no record of it (a 1.12 party has no main
        // tank flag).
        bool isBot;
        BotRole role;
    };

    // The group's tank: the first bot in `members` with the tank role.
    // With none, the leader `leader` when it is a human player in
    // `members`: a player who leads a group of bots with no bot tank tanks
    // the fights. Else NoGuid.
    Guid PickGroupTank(std::vector<GroupMember> const& members, Guid leader);
}

#endif
