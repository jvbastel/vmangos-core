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

bool Hardcore::IsHardcore(uint32 guid)
{
    Status const status = GetStatus(guid);
    return status == Status::Alive || status == Status::Dead;
}

bool Hardcore::IsHardcore(Player const* player)
{
    return player && IsHardcore(player->GetGUIDLow());
}

void Hardcore::SetStatus(uint32 guid, Status status)
{
    // Database: a direct (synchronous) write, not the 15-minute save. Client: none here; the caller tells
    // the player.
    if (CharacterDatabase.DirectPExecute(
            "INSERT INTO `character_hardcore` (`guid`, `status`) VALUES (%u, %u) "
            "ON DUPLICATE KEY UPDATE `status` = VALUES(`status`)", guid, uint32(status)))
        Remember(guid, status);
    else
        Forget(guid);
}

bool Hardcore::CanTakeStatus(Player const* player)
{
    return player
        && player->GetLevel() == 1
        && player->GetUInt32Value(PLAYER_XP) == 0
        && GetStatus(player->GetGUIDLow()) == Status::Never;
}

bool Hardcore::TakeStatus(Player* player)
{
    if (!CanTakeStatus(player))
        return false;
    uint32 const guid = player->GetGUIDLow();
    // Database: a direct write with the time, at once. Client: the NPC script tells the player.
    // A failed INSERT (a database error, or a row the cache did not know) sets nothing: the player is told
    // that the status was not taken, and the next ask reads the row again.
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

std::string Hardcore::StatusLine(Player const* player)
{
    switch (GetStatus(player->GetGUIDLow()))
    {
        case Status::Alive:
            return "Hardcore: alive";
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
        player->SendSysMessage("Hardcore: alive");
}
