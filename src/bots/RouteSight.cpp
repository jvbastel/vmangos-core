#include "RouteSight.h"
#include "BotGeometry.h"

#include <cmath>

namespace Bots
{
    Position NearestMember(Position const& at, GeneratorPack const& pack, std::vector<GeneratorSpawn> const& spawns)
    {
        Position best = spawns[pack.members[0]].position;
        for (std::size_t member : pack.members)
            if (Distance3d(at, spawns[member].position) < Distance3d(at, best))
                best = spawns[member].position;
        return best;
    }

    PackSight SightOf(Position const& at, GeneratorPack const& pack, std::vector<GeneratorSpawn> const& spawns,
        GeneratorSettings const& settings, RouteNav& nav, float range)
    {
        if (DistanceToPack(at, pack, spawns) > range)
            return {Sight::OutOfRange, -1.0f};
        Position const target = NearestMember(at, pack, spawns);
        if (!nav.LineOfSight(at, target))
            return {Sight::Hidden, -1.0f};
        std::vector<Position> path;
        if (!nav.Path(at, target, path))
            return {std::fabs(at.z - target.z) <= settings.levelPathSlack ? Sight::Pullable : Sight::OtherLevel, -1.0f};
        float const length = PathLength(path);
        if (length > settings.levelPathFactor * Distance3d(at, target) + settings.levelPathSlack)
            return {Sight::OtherLevel, length};
        return {Sight::Pullable, length};
    }

    float PullOnPath(RouteLine const& way, GeneratorPack const& pack, std::vector<GeneratorSpawn> const& spawns,
        GeneratorSettings const& settings)
    {
        float const range = settings.aggroRange + settings.margin;
        float first = -1.0f;
        for (float s : way.Samples())
        {
            if (way.Length() - s > settings.pullPathLimit)
                continue;
            if (first < 0.0f)
                first = s;
            if (DistanceToPack(way.At(s), pack, spawns) <= range)
                return s;
        }
        return first < 0.0f ? way.Length() : first;
    }

    bool PullBandSpot(Position const& leader, float angle, GeneratorPack const& pack,
        std::vector<GeneratorSpawn> const& spawns, GeneratorSettings const& settings, RouteNav& nav, Position& spot)
    {
        float const range = settings.aggroRange + settings.margin;
        for (float reach = (settings.aggroRange + range) / 2.0f; reach <= 2.0f * range; reach += settings.margin / 2.0f)
        {
            spot = nav.Reach(leader, std::cos(angle), std::sin(angle), reach);
            float const distance = DistanceToPack(spot, pack, spawns);
            if (distance >= settings.aggroRange && distance <= range)
                return true;
            // Past the band, or stopped short by a wall.
            if (distance > range || Distance2d(spot, leader) < reach - 0.5f)
                return false;
        }
        return false;
    }

    bool PullsPackOfLater(Position const& place, std::vector<std::size_t> const& foughtAt, std::size_t step,
        std::size_t own, std::vector<GeneratorPack> const& packs, std::vector<GeneratorSpawn> const& spawns,
        GeneratorSettings const& settings, RouteNav& nav)
    {
        for (std::size_t p = 0; p < packs.size(); ++p)
            if (p != own && foughtAt[p] > step && DistanceToPack(place, packs[p], spawns) < settings.aggroRange &&
                SightOf(place, packs[p], spawns, settings, nav, settings.aggroRange).sight == Sight::Pullable)
                return true;
        return false;
    }
}
