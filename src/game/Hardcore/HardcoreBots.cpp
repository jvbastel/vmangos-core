/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// Hardcore bots in a hardcore character's group (WOWAI#257, user stories 38-49 and 53 of #250). A hardcore bot
// is a character with the hardcore status, like the player: the same row in character_hardcore, the same
// final death (Hardcore::OnDeath: the lock, the death record and the 60 seconds), the same border rules and
// the same drop (Hardcore::DropStatus drops every hardcore member of the player's group). This file holds
// only what differs for a bot: its status at its creation, the bot manager's group join (which does not
// pass the group invite handler), its login, and its logout 60 seconds after its death (a bot has no
// client to log out).

#include "Hardcore.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "WorldSession.h"

#include <ctime>

namespace
{
    // The same reason as the group invite refusal of HardcoreRules.cpp: one rule, one line for the player.
    char const* const NoGroup =
        "Hardcore: a hardcore character cannot be in a group with a character that is not hardcore.";
    char const* const DeadBot = "Hardcore: this bot is dead for good. A hardcore death is final.";
}

bool Hardcore::MakeBotHardcore(uint32 guid)
{
    // Database: a direct write with the time, at once, as Hardcore::TakeStatus does for the player; then the
    // cache (SetStatus writes the same status once more).
    if (!CharacterDatabase.DirectPExecute(
            "INSERT INTO `character_hardcore` (`guid`, `status`, `taken_at`) VALUES (%u, %u, " UI64FMTD ")",
            guid, uint32(Status::Alive), uint64(time(nullptr))))
        return false;
    SetStatus(guid, Status::Alive);
    return true;
}

bool Hardcore::AllowBotInGroup(Player* player, uint32 botGuid)
{
    char const* reason = nullptr;
    if (GetStatus(botGuid) == Status::Dead)
        reason = DeadBot;
    else if (IsHardcore(player) != IsHardcore(botGuid))
        reason = NoGroup;
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
