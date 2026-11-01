/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// Dropping the hardcore status (WOWAI#255): the player drops it at a hardcore NPC, and every hardcore bot
// of the player's group that is alive drops it with the player. A dropped character is a normal character
// in every way (IsHardcore is false), and the NPC never offers it the status again (CanTakeStatus asks for
// Never).

#include "Hardcore.h"
#include "Database/DatabaseEnv.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <ctime>
#include <string>
#include <vector>

namespace
{
    // Drops the status of `guid` when it is Alive: status Dropped and dropped_at in one direct write that
    // changes only an Alive row, then the cache (Hardcore::ChangeStatus). False when the character is not
    // alive and hardcore, the write failed, or a death on another map thread landed first.
    bool Drop(uint32 guid, uint64 now)
    {
        if (Hardcore::GetStatus(guid) != Hardcore::Status::Alive)
            return false;
        return Hardcore::ChangeStatus(guid, Hardcore::Status::Alive, Hardcore::Status::Dropped,
            ", `dropped_at` = " + std::to_string(now));
    }

    // The members of the player's group other than the player.
    std::vector<ObjectGuid> OtherMembers(Player const* player)
    {
        std::vector<ObjectGuid> members;
        if (Group const* group = player->GetGroup())
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
                if (slot.guid != player->GetObjectGuid())
                    members.push_back(slot.guid);
        return members;
    }
}

bool Hardcore::CanDropStatus(Player const* player)
{
    return GetStatus(player) == Status::Alive;
}

char const* Hardcore::DropRefusal(Player const* player)
{
    if (!player)
        return nullptr;
    for (ObjectGuid const& guid : OtherMembers(player))
        if (!IsBot(guid.GetCounter()) && GetStatus(guid.GetCounter()) == Status::Alive)
            return "You cannot drop the hardcore status in a group with another hardcore player: the drop takes "
                "the hardcore bots of the group along, and no other player's status. Leave the group first.";
    return nullptr;
}

uint32 Hardcore::DropStatus(Player* player)
{
    if (!CanDropStatus(player) || DropRefusal(player))
        return 0;
    uint64 const now = uint64(time(nullptr));
    if (!Drop(player->GetGUIDLow(), now))
        return 0;
    uint32 dropped = 1;

    // The guids first: the loop below sends chat, and the member list must not change under it. Only the
    // bots: DropRefusal let no other hardcore player through, and a dead member keeps its final death.
    for (ObjectGuid const& guid : OtherMembers(player))
    {
        if (!IsBot(guid.GetCounter()) || !Drop(guid.GetCounter(), now))
            continue;
        ++dropped;
        // Client: a system line to the bot when it is online (bot chat may read it; the player's own line
        // comes from the NPC script).
        if (Player* member = ObjectAccessor::FindPlayer(guid))
            member->PSendSysMessage("%s dropped the hardcore status for the group. You dropped the hardcore "
                "status too: this character is a normal character now, and can never take it again.", player->GetName());
    }
    return dropped;
}
