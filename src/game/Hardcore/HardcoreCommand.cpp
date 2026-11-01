/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// The player command `.hardcore` (WOWAI#251): shows the hardcore status of the own character. It sets and
// drops nothing. In the game only (it needs a character).

#include "Hardcore.h"
#include "Chat.h"
#include "Player.h"

bool ChatHandler::HandleHardcoreCommand(char* /*args*/)
{
    Player* player = m_session ? m_session->GetPlayer() : nullptr;
    if (!player)
        return false;
    SendSysMessage(Hardcore::StatusLine(player).c_str());
    return true;
}
