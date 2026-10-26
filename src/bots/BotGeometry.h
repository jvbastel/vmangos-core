#ifndef WOWAI_BOTS_BOT_GEOMETRY_H
#define WOWAI_BOTS_BOT_GEOMETRY_H

#include "BotTypes.h"

#include <algorithm>
#include <cmath>
#include <vector>

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

    // The distance from `p` to the segment from `a` to `b`, with the height.
    inline float DistanceToSegment(Position const& p, Position const& a, Position const& b)
    {
        float const vx = b.x - a.x;
        float const vy = b.y - a.y;
        float const vz = b.z - a.z;
        float const length2 = vx * vx + vy * vy + vz * vz;
        float t = 0.0f;
        if (length2 > 0.0f)
            t = std::max(0.0f, std::min(1.0f, ((p.x - a.x) * vx + (p.y - a.y) * vy + (p.z - a.z) * vz) / length2));
        return Distance3d(p, {a.x + vx * t, a.y + vy * t, a.z + vz * t});
    }

    // The length in yards of the path through `path`, with the height.
    inline float PathLength(std::vector<Position> const& path)
    {
        float length = 0.0f;
        for (std::size_t i = 1; i < path.size(); ++i)
            length += Distance3d(path[i - 1], path[i]);
        return length;
    }
}

#endif
