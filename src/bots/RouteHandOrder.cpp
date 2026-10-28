#include "RouteHandOrder.h"
#include "BotGeometry.h"
#include "DataFields.h"
#include "DataTokenizer.h"
#include "RouteLine.h"
#include "RouteSight.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace Bots
{
    namespace
    {
        std::string PackOfSpawn(std::uint32_t guid)
        {
            return "the pack of spawn " + std::to_string(guid);
        }

        float const TwoPi = 6.2831853f;
        // The directions around a pack in which a pull point is sought.
        int const PullSearchDirections = 16;

        std::size_t PackOf(std::uint32_t guid, std::vector<GeneratorPack> const& packs,
            std::vector<GeneratorSpawn> const& spawns)
        {
            for (std::size_t p = 0; p < packs.size(); ++p)
                for (std::size_t member : packs[p].members)
                    if (spawns[member].guid == guid)
                        return p;
            return packs.size();
        }

        // Where the steps of the hand order go when the route fights none of
        // them: after the point or mark of the main way (no quest, no
        // detour) with the shortest navmesh path to the pack. kept.size()
        // when no path reaches it.
        std::size_t NearestStep(std::vector<RouteEvent> const& kept, Position const& target, RouteNav& nav)
        {
            std::vector<std::pair<float, std::size_t>> nearest; // The straight distance, then the step.
            for (std::size_t e = 0; e < kept.size(); ++e)
                if ((kept[e].kind == RouteEventKind::Point || kept[e].kind == RouteEventKind::Mark) &&
                    kept[e].quest == 0 && kept[e].detour < 0)
                    nearest.push_back({Distance3d(kept[e].center, target), e});
            std::sort(nearest.begin(), nearest.end());
            float best = -1.0f;
            std::size_t at = kept.size();
            for (auto const& candidate : nearest)
            {
                // A path is never shorter than the straight distance.
                if (best >= 0.0f && candidate.first >= best)
                    break;
                std::vector<Position> path;
                if (!nav.Path(kept[candidate.second].center, target, path))
                    continue;
                float const length = PathLength(path);
                if (best < 0.0f || length < best)
                {
                    best = length;
                    at = candidate.second;
                }
            }
            return at;
        }
    }

    PackOrderResult ParsePackOrder(std::string const& text)
    {
        PackOrderResult result;
        for (DataLine const& line : TokenizeLines(text))
        {
            std::string error;
            std::uint32_t guid = 0;
            bool const skip = !line.words.empty() && line.words[0] == "skip";
            if (line.words.size() != 2 || !(skip || line.words[0] == "pack") ||
                !DataFields::ReadNumber(line.words[1], guid) || guid == 0)
                error = "use: pack <spawn guid>, or skip <spawn guid>";
            else if (std::find(result.guids.begin(), result.guids.end(), guid) != result.guids.end() ||
                std::find(result.skips.begin(), result.skips.end(), guid) != result.skips.end())
                error = "the guid " + line.words[1] + " is already in the order";
            if (!error.empty())
            {
                result.guids.clear();
                result.skips.clear();
                result.error = "line " + std::to_string(line.number) + ": " + error;
                return result;
            }
            (skip ? result.skips : result.guids).push_back(guid);
        }
        result.ok = true;
        return result;
    }

    HandOrderPacks ResolveHandOrder(std::vector<std::uint32_t> const& order, std::vector<std::uint32_t> const& skips,
        std::vector<GeneratorPack> const& packs, std::vector<GeneratorSpawn> const& spawns,
        std::vector<std::string>& warnings)
    {
        HandOrderPacks hand;
        hand.skipped.assign(packs.size(), false);
        for (std::uint32_t guid : skips)
        {
            std::size_t const p = PackOf(guid, packs, spawns);
            if (p == packs.size())
                warnings.push_back("the hand order skips spawn " + std::to_string(guid) + ", which is in no pack");
            else
                hand.skipped[p] = true;
        }
        for (std::uint32_t guid : order)
        {
            std::size_t const p = PackOf(guid, packs, spawns);
            if (p == packs.size())
                warnings.push_back("the hand order names spawn " + std::to_string(guid) + ", which is in no pack");
            else if (hand.skipped[p])
                warnings.push_back("the hand order names and skips " + PackOfSpawn(guid) + ": it skips it");
            else if (std::find(hand.order.begin(), hand.order.end(), p) != hand.order.end())
                warnings.push_back("the hand order names " + PackOfSpawn(guid) + " twice");
            else
                hand.order.push_back(p);
        }
        return hand;
    }

    std::vector<RouteEvent> ApplyHandOrder(std::vector<RouteEvent> const& events,
        std::vector<std::size_t> const& order, std::vector<GeneratorPack> const& packs,
        std::vector<GeneratorSpawn> const& spawns, GeneratorSettings const& settings, RouteNav& nav,
        std::vector<std::string>& warnings)
    {
        if (order.empty())
            return events;
        auto rankOf = [&order](RouteEvent const& e) {
            if (e.kind != RouteEventKind::Pull && e.kind != RouteEventKind::Pack)
                return order.size();
            return std::size_t(std::find(order.begin(), order.end(), e.pack) - order.begin());
        };
        auto leaderOf = [&](std::size_t p) { return spawns[packs[p].members[0]]; };

        // The named packs that the route does not fight are added.
        std::vector<bool> added(packs.size(), false);
        for (std::size_t p : order)
            added[p] = true;
        for (RouteEvent const& e : events)
            if (e.kind == RouteEventKind::Pack)
                added[e.pack] = false;

        // The detours that keep a fight.
        std::set<int> detoursLeft;
        for (RouteEvent const& e : events)
            if (e.kind == RouteEventKind::Pack && e.detour >= 0 && rankOf(e) == order.size())
                detoursLeft.insert(e.detour);

        // The steps of the hand order come out of the walk, as a block.
        std::vector<RouteEvent> kept;
        std::vector<std::vector<RouteEvent>> moved(order.size());
        std::size_t at = events.size() + 1; // No step moved yet.
        bool takenAway = false;
        for (RouteEvent const& e : events)
        {
            std::size_t const rank = rankOf(e);
            if (rank < order.size())
            {
                if (at > events.size())
                    at = kept.size();
                RouteEvent step = e;
                step.quest = 0;
                step.detour = -1;
                step.rejoin = true;
                moved[rank].push_back(step);
                takenAway = true;
                continue;
            }
            if (e.kind == RouteEventKind::Point && e.detour >= 0 && !detoursLeft.count(e.detour))
            {
                takenAway = true;
                continue;
            }
            kept.push_back(e);
            if (takenAway && e.kind != RouteEventKind::Pack)
            {
                kept.back().rejoin = true;
                takenAway = false;
            }
        }
        for (std::size_t r = 0; r < order.size(); ++r)
        {
            std::size_t const p = order[r];
            if (!added[p])
                continue;
            // The pull point gets its place below.
            RouteEvent pull{0.0f, 0, 0, RouteEventKind::Pull, leaderOf(p).position};
            pull.pack = p;
            pull.rejoin = true;
            RouteEvent fight{0.0f, 0, 0, RouteEventKind::Pack, leaderOf(p).position};
            fight.pack = p;
            moved[r] = {pull, fight};
        }
        if (at > events.size())
        {
            std::size_t const nearest = NearestStep(kept, leaderOf(order[0]).position, nav);
            if (nearest == kept.size())
            {
                for (std::size_t p : order)
                    warnings.push_back("the hand order adds " + PackOfSpawn(leaderOf(p).guid) +
                        ", but no navmesh path from the route reaches it: it is off the route");
                return events;
            }
            at = nearest + 1;
        }
        std::vector<RouteEvent> block;
        for (std::vector<RouteEvent> const& steps : moved)
            block.insert(block.end(), steps.begin(), steps.end());
        kept.insert(kept.begin() + std::ptrdiff_t(at), block.begin(), block.end());

        // A pull point of the builder suits the order of the line. In the
        // hand order, a pack fought later may still live near it. Then the
        // pull point moves to a spot around its pack, out of the aggro range
        // of the pack and within the pull range, that pulls no pack of later:
        // the one with the shortest walk from the step before. An added pack
        // gets its pull point the same way, or on the path to it.
        std::vector<std::size_t> foughtAt(packs.size(), kept.size());
        for (std::size_t e = 0; e < kept.size(); ++e)
            if (kept[e].kind == RouteEventKind::Pack)
                foughtAt[kept[e].pack] = e;
        float const range = settings.aggroRange + settings.margin;
        std::set<std::size_t> dropped; // Added packs that no path reaches.
        bool hasLast = false;
        Position last{};
        for (std::size_t e = 0; e < kept.size(); ++e)
        {
            RouteEvent& step = kept[e];
            std::size_t const p = step.pack;
            if (step.kind == RouteEventKind::Pull && rankOf(step) < order.size() &&
                (added[p] || PullsPackOfLater(step.center, foughtAt, e, p, packs, spawns, settings, nav)))
            {
                Position const leader = leaderOf(p).position;
                std::string const pack = PackOfSpawn(leaderOf(p).guid);
                float best = -1.0f;
                for (int k = 0; k < PullSearchDirections; ++k)
                {
                    Position place;
                    if (!PullBandSpot(leader, float(k) * TwoPi / float(PullSearchDirections), packs[p], spawns,
                            settings, nav, place))
                        continue;
                    PackSight const sight = SightOf(place, packs[p], spawns, settings, nav, range);
                    if (sight.sight != Sight::Pullable || sight.path > settings.pullPathLimit ||
                        PullsPackOfLater(place, foughtAt, e, p, packs, spawns, settings, nav))
                        continue;
                    float walk = 0.0f;
                    std::vector<Position> path;
                    if (hasLast)
                    {
                        if (!nav.Path(last, place, path))
                            continue;
                        walk = PathLength(path);
                    }
                    if (best < 0.0f || walk < best)
                    {
                        best = walk;
                        step.center = place;
                    }
                }
                std::vector<Position> path;
                if (best >= 0.0f)
                {
                    // Sideways is across the way to the pack.
                    float const length = std::max(1e-3f, Distance2d(step.center, leader));
                    step.dx = (leader.x - step.center.x) / length;
                    step.dy = (leader.y - step.center.y) / length;
                    if (!added[p])
                        warnings.push_back("the hand order moves the pull point of " + pack + " to " +
                            DataFields::FormatPosition(step.center) + ": the pull point of the line pulls a pack fought later");
                }
                else if (!added[p])
                    warnings.push_back("the hand order: no pull point around " + pack +
                        " keeps out of the packs fought later; it keeps the pull point of the line at " +
                        DataFields::FormatPosition(step.center));
                else if (hasLast && nav.Path(last, leader, path))
                {
                    RouteLine const way(path);
                    float const s = PullOnPath(way, packs[p], spawns, settings);
                    step.center = way.At(s);
                    way.Direction(s, step.dx, step.dy);
                    warnings.push_back("the hand order adds " + pack + " with the pull point " +
                        DataFields::FormatPosition(step.center) + " on the path to it: no spot around it keeps out of the packs "
                        "fought later");
                }
                else
                {
                    warnings.push_back("the hand order adds " + pack +
                        ", but no navmesh path from the step before reaches it: it is off the route");
                    dropped.insert(p);
                    continue;
                }
            }
            if (step.kind != RouteEventKind::Pack && !(step.kind == RouteEventKind::Pull && dropped.count(p)))
            {
                hasLast = true;
                last = step.center;
            }
        }

        // The points of the walk where it changed, along the navmesh path.
        // They belong to the quest and the detour of the step they lead to.
        std::vector<RouteEvent> result;
        hasLast = false;
        bool lastRejoin = false;
        for (RouteEvent const& e : kept)
        {
            if ((e.kind == RouteEventKind::Pull || e.kind == RouteEventKind::Pack) && dropped.count(e.pack))
                continue;
            if (e.kind == RouteEventKind::Pack)
            {
                result.push_back(e);
                continue;
            }
            std::vector<Position> path;
            if (hasLast && (e.rejoin || lastRejoin) && nav.Path(last, e.center, path))
            {
                RouteLine const way(path);
                for (float s = settings.pointSpacing; s < way.Length() - settings.pointSpacing / 2.0f;
                     s += settings.pointSpacing)
                {
                    RouteEvent point{e.s, e.order, e.sub, RouteEventKind::Point, way.At(s)};
                    way.Direction(s, point.dx, point.dy);
                    point.quest = e.quest;
                    point.detour = e.detour;
                    result.push_back(point);
                }
            }
            result.push_back(e);
            hasLast = true;
            last = e.center;
            lastRejoin = e.rejoin;
        }
        return result;
    }

    std::vector<std::string> SkippedPackWarnings(Route const& route, std::vector<GeneratorPack> const& packs,
        std::vector<GeneratorSpawn> const& spawns, std::vector<bool> const& skipped, float aggroRange)
    {
        std::vector<RoutePoint const*> points;
        for (RouteStep const& step : route.steps)
            if (step.kind == RouteStepKind::Point)
                points.push_back(&route.points[step.index]);
        std::vector<std::string> warnings;
        for (std::size_t p = 0; p < packs.size(); ++p)
        {
            if (!skipped[p])
                continue;
            float nearest = 1e9f;
            std::string near;
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                Position const from = points[i]->places[0].position;
                Position const to = points[i + 1 < points.size() ? i + 1 : i]->places[0].position;
                RouteLine const walk({from, to});
                for (float s = 0.0f; s <= walk.Length(); s += 1.0f)
                {
                    float const distance = DistanceToPack(walk.At(s), packs[p], spawns);
                    if (distance < nearest)
                    {
                        nearest = distance;
                        near = points[i]->name;
                    }
                }
            }
            if (nearest >= aggroRange)
                continue;
            char text[240];
            std::snprintf(text, sizeof(text),
                "the route passes the skipped pack of spawn %u at %.0f yards, within its aggro range of %.0f, by "
                "the point %s: it can join a fight as an extra pack",
                unsigned(spawns[packs[p].members[0]].guid), nearest, aggroRange, near.c_str());
            warnings.push_back(text);
        }
        return warnings;
    }
}
