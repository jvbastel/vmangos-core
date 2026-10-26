/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "GameRouteNav.h"
#include "MoveMap.h"
#include "MoveMapSharedDefines.h"
#include "PathFinder.h"
#include "Map.h"
#include "Player.h"

#include <algorithm>
#include <cmath>

namespace
{
    float Distance(Bots::Position const& a, Bots::Position const& b)
    {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    }
}

GameRouteNav::GameRouteNav(Player* walker) : m_walker(walker)
{
    m_query = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMeshQuery(walker->GetMapId());
    m_ground.setIncludeFlags(NAV_GROUND);
    m_ground.setExcludeFlags(NAV_MAGMA | NAV_SLIME);
    m_lava.setIncludeFlags(NAV_MAGMA | NAV_SLIME);
}

bool GameRouteNav::Path(Bots::Position const& from, Bots::Position const& to, std::vector<Bots::Position>& out)
{
    out.clear();
    // PathInfo finds no path between two spots this close: its
    // smooth path needs two steps.
    if (Distance(from, to) < 2.0f && OnMesh(from) && OnMesh(to) && !OnLava(from) && !OnLava(to))
    {
        out = {from, to};
        return true;
    }
    Bots::Position start = from;
    // A long path comes in parts: PathInfo stops at its point limit.
    for (int part = 0; part < 8; ++part)
    {
        PathInfo path(m_walker);
        path.setUseStrightPath(m_straight);
        path.calculate(Vector3(start.x, start.y, start.z), Vector3(to.x, to.y, to.z), false);
        uint32 const type = path.getPathType();
        if (type & (PATHFIND_NOPATH | PATHFIND_SHORTCUT | PATHFIND_NOT_USING_PATH))
            return false;
        PointsArray const& points = path.getPath();
        if (points.empty())
            return false;
        for (Vector3 const& point : points)
        {
            Bots::Position const position{point.x, point.y, point.z};
            if (out.empty() || Distance(out.back(), position) > 0.01f)
                out.push_back(position);
        }
        if (!(type & PATHFIND_INCOMPLETE))
            break;
        if (Distance(out.back(), start) < 1.0f)
            return false;
        start = out.back();
    }
    Bots::Position const& end = out.back();
    if (std::hypot(end.x - to.x, end.y - to.y) > 3.0f || std::fabs(end.z - to.z) > 5.0f)
        return false;
    // No step of the path on lava.
    for (std::size_t i = 1; i < out.size(); ++i)
    {
        float const length = Distance(out[i - 1], out[i]);
        int const steps = std::max(1, int(length / 0.5f));
        for (int k = 0; k <= steps; ++k)
        {
            float const t = float(k) / float(steps);
            Bots::Position const p{out[i - 1].x + (out[i].x - out[i - 1].x) * t,
                out[i - 1].y + (out[i].y - out[i - 1].y) * t, out[i - 1].z + (out[i].z - out[i - 1].z) * t};
            if (OnLava(p))
                return false;
        }
    }
    return true;
}

Bots::Position GameRouteNav::Reach(Bots::Position const& from, float dx, float dy, float distance)
{
    float const norm = std::hypot(dx, dy);
    if (!m_query || norm < 1e-4f || distance <= 0.0f)
        return from;
    float start[3] = {from.y, from.z, from.x};
    float extents[3] = {2.0f, 5.0f, 2.0f};
    float nearest[3];
    dtPolyRef startRef = 0;
    if (dtStatusFailed(m_query->findNearestPoly(start, extents, &m_ground, &startRef, nearest)) || !startRef)
        return from;
    float end[3] = {nearest[0] + dy / norm * distance, nearest[1], nearest[2] + dx / norm * distance};
    float t = 0.0f;
    float normal[3];
    dtPolyRef polys[128];
    int count = 0;
    if (dtStatusFailed(m_query->raycast(startRef, nearest, end, &m_ground, &t, normal, polys, &count, 128)))
        return from;
    float const along = t > 1.0f ? 1.0f : std::max(0.0f, t - 0.05f / distance);
    float hit[3] = {nearest[0] + (end[0] - nearest[0]) * along, nearest[1], nearest[2] + (end[2] - nearest[2]) * along};
    float height = from.z;
    if (count > 0)
    {
        for (int i = count - 1; i >= 0; --i)
            if (dtStatusSucceed(m_query->getPolyHeight(polys[i], hit, &height)))
                break;
    }
    return {hit[2], hit[0], height};
}

bool GameRouteNav::OnMesh(Bots::Position const& position)
{
    return Inside(position, m_ground);
}

bool GameRouteNav::LineOfSight(Bots::Position const& from, Bots::Position const& to)
{
    // The eye height of a player, as in WorldObject::IsWithinLOSAtPosition.
    float const eyes = 2.0f;
    return m_walker->GetMap()->isInLineOfSight(from.x, from.y, from.z + eyes, to.x, to.y, to.z + eyes);
}

bool GameRouteNav::OnLava(Bots::Position const& position)
{
    return Inside(position, m_lava) && !Inside(position, m_ground);
}

bool GameRouteNav::Inside(Bots::Position const& position, dtQueryFilter const& filter)
{
    if (!m_query)
        return false;
    float point[3] = {position.y, position.z, position.x};
    float extents[3] = {0.5f, 3.0f, 0.5f};
    float closest[3];
    dtPolyRef ref = 0;
    if (dtStatusFailed(m_query->findNearestPoly(point, extents, &filter, &ref, closest)) || !ref)
        return false;
    return std::hypot(closest[0] - point[0], closest[2] - point[2]) < 0.1f &&
        std::fabs(closest[1] - point[1]) < 3.0f;
}
