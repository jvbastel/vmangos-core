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
    // With none, the leader `leader` when it is a human in `members`, of any
    // class (jvbastel/WOWAI#189, the player tank): a human who leads a group
    // of bots with no bot tank tanks the fights. Else NoGuid.
    Guid PickGroupTank(std::vector<GroupMember> const& members, Guid leader);

    // jvbastel/WOWAI#189: `tank` is a human in `members`. The group of such
    // a tank is a player-tank group (GroupCoordinator::IsPlayerTankGroup).
    bool IsHumanTank(std::vector<GroupMember> const& members, Guid tank);

    // The tank that the group coordinator gets now, when its tank is
    // `current`: the pick of PickGroupTank. With no pick, or a human pick,
    // `current` stays when it is a bot tank (`currentIsBotTank`): the tank
    // bot sets itself, also from another map, and a bot-tank group does not
    // become a player-tank group while its tank is away. A human tank who gives up the lead, or
    // leaves the map, is no tank any more.
    Guid NextGroupTank(std::vector<GroupMember> const& members, Guid leader, Guid current, bool currentIsBotTank);
}

#endif
