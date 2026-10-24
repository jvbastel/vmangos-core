/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/


#ifndef WOWAI_GAME_BOTHOOKS_H
#define WOWAI_GAME_BOTHOOKS_H

class Player;

// The calls of the WOWAI bots in upstream core files. Each hook is one line
// in an upstream file and does nothing for a player who is not our bot.
namespace BotHooks
{
    // At the end of Player::GiveLevel: runs the level routine for our bots.
    void OnLevelUp(Player* player);
}

#endif
