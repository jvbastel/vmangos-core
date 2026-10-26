/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_ROUTE_TRACES_H
#define WOWAI_GAME_ROUTE_TRACES_H

#include "Common.h"

// The GM traces of `.route trace` (RouteTraceCommands.cpp).
namespace RouteTraces
{
    // From BotMgr::Update on the world thread: each trace that is on looks
    // at the position of its character.
    void Update(uint32 diff);
}

#endif
