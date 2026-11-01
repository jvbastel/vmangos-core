/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// No escape from a fight by a lost connection (WOWAI#256, user stories 34-37 of #250). The play realm keeps
// ForceLogoutDelay = 0, so a character whose client dies leaves the world at once. A hardcore character in
// combat gets the stock delayed logout instead: it stays in the world, and a death in that time is its final
// death (the death hook of HardcoreDeath.cpp runs as for any death).

#include "Hardcore.h"
#include "Log.h"
#include "Player.h"

bool Hardcore::KeepInWorldAfterLostConnection(Player const* player)
{
    if (!player || !player->IsInCombat() || GetStatus(player->GetGUIDLow()) != Status::Alive)
        return false;
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "Hardcore: %s (guid %u) lost its connection in combat: it stays in the world "
        "for the logout delay", player->GetName(), player->GetGUIDLow());
    return true;
}
