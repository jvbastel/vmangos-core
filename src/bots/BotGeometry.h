#ifndef WOWAI_BOTS_BOT_GEOMETRY_H
#define WOWAI_BOTS_BOT_GEOMETRY_H

#include "BotTypes.h"

#include <cmath>

// Small geometry helpers that the bot library shares.
namespace Bots
{
    float const Pi = 3.14159265f;

    // The distance in yards from `a` to `b` on the map, without the height.
    inline float Distance2d(Position const& a, Position const& b)
    {
        return std::hypot(b.x - a.x, b.y - a.y);
    }

    // The distance in yards from `a` to `b`, with the height.
    inline float Distance3d(Position const& a, Position const& b)
    {
        float const dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
}

#endif
