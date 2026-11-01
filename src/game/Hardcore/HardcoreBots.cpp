/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// Hardcore bots in a hardcore character's group (WOWAI#257, user stories 38-49 and 53 of #250). A hardcore bot
// is a character with the hardcore status, like the player: the same row in character_hardcore, the same
// final death (Hardcore::OnDeath: the lock, the death record and the 60 seconds), the same border rules and
// the same drop (Hardcore::DropStatus drops every hardcore bot of the player's group). This file holds
// only what differs for a bot: its status at its creation, the bot manager's group join (which does not
// pass the group invite handler), its login, and its logout 60 seconds after its death (a bot has no
// client to log out).

#include "Hardcore.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldSession.h"

namespace
{
    char const* const DeadBot = "Hardcore: this bot is dead for good. A hardcore death is final.";
    char const* const BotAboveLevel =
        "Hardcore: a hardcore bot above your level cannot join your group. A hardcore bot is at or below your level.";
}

bool Hardcore::MakeBotHardcore(uint32 guid)
{
    return InsertAlive(guid);
}

bool Hardcore::IsBot(uint32 guid)
{
    return sBotMgr.IsBotCharacter(ObjectGuid(HIGHGUID_PLAYER, guid));
}

char const* Hardcore::BotLevelRefusal(Player const* player, uint32 botGuid)
{
    if (!IsHardcore(player) || !IsHardcore(botGuid))
        return nullptr;
    // The level of the bot in the world, or of its saved character (the player cache, then the database).
    ObjectGuid const guid(HIGHGUID_PLAYER, botGuid);
    Player const* bot = ObjectAccessor::FindPlayer(guid);
    uint32 const level = bot ? bot->GetLevel() : Player::GetLevelFromDB(guid);
    return level > player->GetLevel() ? BotAboveLevel : nullptr;
}

bool Hardcore::AllowBotInGroup(Player* player, uint32 botGuid)
{
    // A dead bot first: its own reason, also for a player that is not hardcore.
    char const* reason = GetStatus(botGuid) == Status::Dead ? DeadBot : GroupRefusal(botGuid, player->GetGUIDLow());
    if (!reason)
        reason = BotLevelRefusal(player, botGuid);
    if (!reason)
        return true;
    // Client: the reason line to the player. Database: nothing changes; the bot does not log in for it.
    player->SendSysMessage(reason);
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "Hardcore: refused bot guid %u in the group of %s (guid %u): %s", botGuid,
        player->GetName(), player->GetGUIDLow(), reason);
    return false;
}

bool Hardcore::AllowBotLogin(uint32 guid, std::string& error)
{
    if (GetStatus(guid) != Status::Dead)
        return true;
    // The core refuses the locked character in LoadFromDB too; this line tells the GM or the player why.
    error = DeadBot;
    return false;
}

bool Hardcore::LogOutDeadBot(Player* player)
{
    // Only a WOWAI bot (BotAI): a stock PlayerBot keeps the normal logout, as the bot manager does not know it.
    PlayerBotEntry const* entry = player->GetSession()->GetBot();
    if (!entry || !dynamic_cast<BotAI const*>(entry->ai.get()))
        return false;
    // The bot manager runs on the world thread, this event on a map thread: it leaves the group and logs out
    // at the bot manager's next update.
    sBotMgr.QueueFinalLogout(player->GetObjectGuid());
    return true;
}
