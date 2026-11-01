/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// Dropping the hardcore status (WOWAI#255): the player drops it at a hardcore NPC, and every hardcore member
// of the player's group that is alive drops it with the player. A dropped character is a normal character
// in every way (IsHardcore is false), and the NPC never offers it the status again (CanTakeStatus asks for
// Never).

#include "Hardcore.h"
#include "Database/DatabaseEnv.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <ctime>
#include <vector>

namespace
{
    // Drops the status of `guid` when it is Alive: status Dropped and dropped_at in one direct write, then
    // the cache. False when the character is not alive and hardcore, or the write failed.
    bool Drop(uint32 guid, uint64 now)
    {
        if (Hardcore::GetStatus(guid) != Hardcore::Status::Alive)
            return false;
        // Database: one direct (synchronous) UPDATE, so the status and its time land together and at once.
        // Only an Alive row changes: when two members drop at once on two map threads, the first time stays.
        if (!CharacterDatabase.DirectPExecute(
                "UPDATE `character_hardcore` SET `status` = %u, `dropped_at` = " UI64FMTD " WHERE `guid` = %u AND `status` = %u",
                uint32(Hardcore::Status::Dropped), now, guid, uint32(Hardcore::Status::Alive)))
            return false;
        // The cache. SetStatus writes the same status once more; the module has no cache-only setter.
        Hardcore::SetStatus(guid, Hardcore::Status::Dropped);
        return true;
    }
}

bool Hardcore::CanDropStatus(Player const* player)
{
    return player && GetStatus(player->GetGUIDLow()) == Status::Alive;
}

uint32 Hardcore::DropStatus(Player* player)
{
    if (!CanDropStatus(player))
        return 0;
    uint64 const now = uint64(time(nullptr));
    if (!Drop(player->GetGUIDLow(), now))
        return 0;
    uint32 dropped = 1;

    Group const* group = player->GetGroup();
    if (!group)
        return dropped;
    // The guids first: the loop below sends chat, and the member list must not change under it.
    std::vector<ObjectGuid> members;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
        if (slot.guid != player->GetObjectGuid())
            members.push_back(slot.guid);
    for (ObjectGuid const& guid : members)
    {
        if (!Drop(guid.GetCounter(), now))
            continue;
        ++dropped;
        // Client: a system line to the member when it is online (a bot reads nothing; the player's own
        // line comes from the NPC script).
        if (Player* member = ObjectAccessor::FindPlayer(guid))
            member->PSendSysMessage("%s dropped the hardcore status for the group. You dropped the hardcore "
                "status too: this character is a normal character now, and can never take it again.", player->GetName());
    }
    return dropped;
}
