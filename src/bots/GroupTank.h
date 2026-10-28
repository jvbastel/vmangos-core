#ifndef WOWAI_BOTS_GROUP_TANK_H
#define WOWAI_BOTS_GROUP_TANK_H

#include "BotRecipe.h"
#include "BotTypes.h"
#include "RoleChoice.h"

#include <vector>

// Review of spec #131, story 10: who the group's tank is
// (GroupCoordinator::GetTank).
namespace Bots
{
    // A member of the group, as the game layer knows it
    // (BotMgr::GroupMembersOf): each bot of the bot system wherever it is
    // (on another map, in a loading screen, offline), and each human in the
    // world, on any map.
    struct GroupMember
    {
        Guid guid;
        // A bot of the bot system, with role `role`. A human player has no
        // role: the game keeps no record of it (a 1.12 party has no main
        // tank flag).
        bool isBot;
        BotRole role;
        // jvbastel/WOWAI#197: the role choice of a human, from the role
        // choice add-on. None for a bot, and for a human with no add-on or
        // no pick.
        RoleChoice choice = RoleChoice::None;
    };

    // The group's tank: the first human in `members` whose role choice is
    // tank (jvbastel/WOWAI#197). Else the first bot with the tank role.
    // With none, the leader `leader` when it is a human in `members`, of any
    // class (jvbastel/WOWAI#189, the player tank): a human who leads a group
    // of bots with no bot tank tanks the fights. Else NoGuid.
    Guid PickGroupTank(std::vector<GroupMember> const& members, Guid leader);

    // jvbastel/WOWAI#189: `tank` is a human in `members`. The group of such
    // a tank is a player-tank group (GroupCoordinator::IsPlayerTankGroup).
    bool IsPlayerTank(std::vector<GroupMember> const& members, Guid tank);

    struct GroupTank
    {
        Guid tank;       // NoGuid: the group has no tank.
        bool playerTank; // The tank is a human (IsPlayerTank).
    };

    // Spec #188 fix-up (S5): the ONE rule of who tanks a group, for the
    // group coordinator (BotAI) and the dungeon follow (BotMgr) alike:
    // PickGroupTank over all of the group's members (see GroupMember). A bot
    // tank of the group is its tank wherever it is, so a human (or the test
    // driver of a scripted run) is the tank only in a group with no bot tank
    // at all, or with its role choice tank.
    GroupTank GroupTankOf(std::vector<GroupMember> const& members, Guid leader);

    // jvbastel/WOWAI#197: never two tanks. A human in `members` picked tank
    // (role choice): the group takes no bot with the tank role.
    bool RefusesBotTank(std::vector<GroupMember> const& members);

    // jvbastel/WOWAI#197: `bot` is a bot with the tank role in `members`,
    // and the group refuses a bot tank (RefusesBotTank): it leaves.
    bool MustLeaveAsSecondTank(std::vector<GroupMember> const& members, Guid bot);

    // jvbastel/WOWAI#197: the one party chat line of a bot tank that leaves
    // (MustLeaveAsSecondTank).
    char const* const SecondTankLeaveLine = "You tank now, so I leave the group.";
}

#endif
