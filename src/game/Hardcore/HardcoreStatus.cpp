/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// The hardcore status of a character (WOWAI#251): the row of character_hardcore, its cache, taking the
// status at the hardcore NPC, the `.hardcore` line and the login line.

#include "Hardcore.h"
#include "Database/DatabaseEnv.h"
#include "Group.h"
#include "Player.h"

#include <ctime>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace
{
    // The rows read so far, by low guid. Map threads ask at once (gossip, chat commands, logins), so the
    // cache is under a lock. The database query runs outside the lock.
    std::mutex cacheLock;
    std::unordered_map<uint32, Hardcore::Status> cache;

    Hardcore::Status ReadStatus(uint32 guid)
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT `status` FROM `character_hardcore` WHERE `guid` = %u", guid));
        if (!result)
            return Hardcore::Status::Never;
        uint8 const status = result->Fetch()[0].GetUInt8();
        if (status > uint8(Hardcore::Status::Dropped))
            return Hardcore::Status::Never;
        return Hardcore::Status(status);
    }

    void Remember(uint32 guid, Hardcore::Status status)
    {
        std::lock_guard<std::mutex> guard(cacheLock);
        cache[guid] = status;
    }

    // A read that ran outside the lock must not overwrite a write that landed meanwhile: it only fills an
    // empty place. Returns the status the cache holds afterwards.
    Hardcore::Status RememberRead(uint32 guid, Hardcore::Status status)
    {
        std::lock_guard<std::mutex> guard(cacheLock);
        return cache.emplace(guid, status).first->second;
    }

    // After a failed write the cache may disagree with the database: the next ask reads the row again.
    void Forget(uint32 guid)
    {
        std::lock_guard<std::mutex> guard(cacheLock);
        cache.erase(guid);
    }
}

Hardcore::Status Hardcore::GetStatus(uint32 guid)
{
    {
        std::lock_guard<std::mutex> guard(cacheLock);
        auto const found = cache.find(guid);
        if (found != cache.end())
            return found->second;
    }
    return RememberRead(guid, ReadStatus(guid));
}

Hardcore::Status Hardcore::GetStatus(Player const* player)
{
    return player ? GetStatus(player->GetGUIDLow()) : Status::Never;
}

bool Hardcore::IsHardcore(uint32 guid)
{
    Status const status = GetStatus(guid);
    return status == Status::Alive || status == Status::Dead;
}

bool Hardcore::IsHardcore(Player const* player)
{
    return player && IsHardcore(player->GetGUIDLow());
}

bool Hardcore::InsertAlive(uint32 guid)
{
    // Database: a direct (synchronous) write with the time, not the 15-minute save. Client: none here; the
    // caller tells the player. A failed INSERT (a database error, or a row the cache did not know) sets
    // nothing, and the next ask reads the row again.
    if (!CharacterDatabase.DirectPExecute(
            "INSERT INTO `character_hardcore` (`guid`, `status`, `taken_at`) VALUES (%u, %u, " UI64FMTD ")",
            guid, uint32(Status::Alive), uint64(time(nullptr))))
    {
        Forget(guid);
        return false;
    }
    Remember(guid, Status::Alive);
    return true;
}

bool Hardcore::ChangeStatus(uint32 guid, Status from, Status to, std::string const& columns)
{
    // Database: one direct (synchronous) UPDATE that only changes a row with the status `from`. The driver
    // does not return the changed row count, so the row is read back: when another map thread changed the
    // status first (a death against a drop), the status read is not `to`, and nothing of this call landed.
    if (!CharacterDatabase.DirectPExecute(
            "UPDATE `character_hardcore` SET `status` = %u%s WHERE `guid` = %u AND `status` = %u",
            uint32(to), columns.c_str(), guid, uint32(from)))
    {
        Forget(guid);
        return false;
    }
    if (ReadStatus(guid) != to)
    {
        Forget(guid);
        return false;
    }
    Remember(guid, to);
    return true;
}

bool Hardcore::CanTakeStatus(Player const* player)
{
    return player
        && player->GetLevel() == 1
        && player->GetUInt32Value(PLAYER_XP) == 0
        && GetStatus(player->GetGUIDLow()) == Status::Never;
}

char const* Hardcore::TakeRefusal(Player const* player)
{
    Group const* group = player ? player->GetGroup() : nullptr;
    if (!group)
        return nullptr;
    // The status would put the group across the hardcore border: every other member must be hardcore
    // already (the group rule of Group::AddMember, which this change of status does not pass).
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
        if (slot.guid != player->GetObjectGuid() && !IsHardcore(slot.guid.GetCounter()))
            return "You cannot take the hardcore status in a group with a character that is not hardcore. "
                "Leave the group first.";
    return nullptr;
}

bool Hardcore::TakeStatus(Player* player)
{
    if (!CanTakeStatus(player) || TakeRefusal(player))
        return false;
    // Client: the NPC script tells the player.
    return InsertAlive(player->GetGUIDLow());
}

std::string Hardcore::StatusLine(Player const* player)
{
    switch (GetStatus(player))
    {
        case Status::Alive:
            return AliveLine;
        case Status::Dead:
            return "Hardcore: dead";
        case Status::Dropped:
            return "This character is not hardcore. It dropped the hardcore status before, and can never take it again.";
        case Status::Never:
        default:
            return "This character is not hardcore. It was never hardcore.";
    }
}

void Hardcore::OnLogin(Player* player)
{
    uint32 const guid = player->GetGUIDLow();
    Status const status = ReadStatus(guid);
    Remember(guid, status);
    if (status == Status::Alive)
        player->SendSysMessage(AliveLine);
}
