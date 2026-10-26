/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_ROUTE_FILES_H
#define WOWAI_GAME_ROUTE_FILES_H

#include "Common.h"
#include "Route.h"

#include <string>

// The route files of <Bots.DataDir>/routes/, which the recorder, the
// generator, the trace, and the route walk of the GM commands share.
namespace RouteFiles
{
    // <Bots.DataDir>/routes/<route>.txt
    std::string PathOf(std::string const& route);
    // <Bots.DataDir>/routes/<route>.trace.txt: the GM trace of the route
    // (bots/RouteTrace.h).
    std::string TracePathOf(std::string const& route);
    // A file of the routes folder that holds a trace, not a route.
    bool IsTraceFile(std::string const& fileName);
    bool Read(std::string const& path, std::string& text);
    // Writes a temporary file first, so that a crash never leaves half a file.
    bool Write(std::string const& path, std::string const& text);
    // The creature, quest, and item names for the comments of a route file.
    Bots::RouteNames Names();
    std::string EntryName(uint32 entry);
}

// The GM traces of `.route trace` (RouteTraceCommands.cpp).
namespace RouteTraces
{
    // From BotMgr::Update on the world thread: each trace that is on looks
    // at the position of its character.
    void Update(uint32 diff);
}

#endif
