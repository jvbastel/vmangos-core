#include "GhostWalk.h"

#include <cmath>
#include <limits>

namespace Bots
{
    namespace
    {
        Position PointAt(Route const& route, std::size_t index)
        {
            return route.points[index].places.front().position;
        }

        float Distance2d(Position const& a, Position const& b)
        {
            return std::hypot(a.x - b.x, a.y - b.y);
        }

        // The path turns at `at` by more than about 20 degrees.
        bool IsTurn(Position const& before, Position const& at, Position const& after)
        {
            float const ax = at.x - before.x, ay = at.y - before.y;
            float const bx = after.x - at.x, by = after.y - at.y;
            float const lengths = std::hypot(ax, ay) * std::hypot(bx, by);
            return lengths > 0.0f && (ax * bx + ay * by) / lengths < 0.94f;
        }
    }

    std::string GhostRouteName(std::uint32_t dungeonMap)
    {
        return "ghost_" + std::to_string(dungeonMap);
    }

    bool IsGhostRouteName(std::string const& name, std::uint32_t& dungeonMap)
    {
        std::string const prefix = "ghost_";
        if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0 || name.size() > prefix.size() + 9)
            return false;
        std::uint32_t map = 0;
        for (std::size_t i = prefix.size(); i < name.size(); ++i)
        {
            if (name[i] < '0' || name[i] > '9')
                return false;
            map = map * 10 + std::uint32_t(name[i] - '0');
        }
        dungeonMap = map;
        return true;
    }

    Route MakeGhostRoute(std::uint32_t dungeonMap, std::uint32_t mapId, std::vector<Position> const& path, float spacing)
    {
        Route route;
        route.name = GhostRouteName(dungeonMap);
        route.mapId = mapId;
        auto add = [&route](Position const& at) {
            route.steps.push_back({RouteStepKind::Point, route.points.size()});
            RoutePoint point;
            point.name = "g" + std::to_string(route.points.size() + 1);
            point.places.push_back({at, GhostPointRadiusYards});
            route.points.push_back(point);
        };
        if (path.empty())
            return route;

        add(path.front());
        float sinceLast = 0.0f;
        for (std::size_t i = 1; i < path.size(); ++i)
        {
            Position const& from = path[i - 1];
            Position const& to = path[i];
            float const length = Distance2d(from, to);
            // Points on the straight part, every `spacing` yards.
            float along = spacing - sinceLast;
            for (; along < length; along += spacing)
            {
                float const t = along / length;
                add({from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, from.z + (to.z - from.z) * t});
            }
            sinceLast = length - (along - spacing);
            bool const last = i + 1 == path.size();
            if (last || IsTurn(from, to, path[i + 1]))
            {
                if (Distance2d(route.points.back().places.front().position, to) > 0.01f)
                    add(to);
                sinceLast = 0.0f;
            }
        }
        route.points.back().name = "portal";
        return route;
    }

    GhostStep NextGhostStep(Route const& route, Position const& at)
    {
        std::size_t const last = route.points.size() - 1;
        // At a point: the last such point counts, so a loop in the route
        // never sends the ghost back.
        for (std::size_t i = last + 1; i-- > 0;)
            if (Distance2d(at, PointAt(route, i)) <= GhostPointYards)
                return i == last ? GhostStep{true, PointAt(route, i)} : GhostStep{false, PointAt(route, i + 1)};

        std::size_t nearest = 0;
        float nearestYards = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i <= last; ++i)
        {
            float const yards = Distance2d(at, PointAt(route, i));
            if (yards < nearestYards)
            {
                nearest = i;
                nearestYards = yards;
            }
        }
        if (nearest < last)
        {
            Position const here = PointAt(route, nearest);
            Position const ahead = PointAt(route, nearest + 1);
            // Past the nearest point, on the way to the next one.
            if ((at.x - here.x) * (ahead.x - here.x) + (at.y - here.y) * (ahead.y - here.y) > 0.0f)
                return GhostStep{false, ahead};
        }
        return GhostStep{false, PointAt(route, nearest)};
    }
}
