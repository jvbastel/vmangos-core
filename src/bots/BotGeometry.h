#ifndef WOWAI_BOTS_BOT_GEOMETRY_H
#define WOWAI_BOTS_BOT_GEOMETRY_H

#include "BotTypes.h"

#include <cmath>

// Small geometry helpers that the bot library shares.
namespace Bots
{
    // The distance in yards from `a` to `b` on the map, without the height.
    inline float Distance2d(Position const& a, Position const& b)
    {
        return std::hypot(b.x - a.x, b.y - a.y);
    }
}

#endif
