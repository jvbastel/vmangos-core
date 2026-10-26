#include "RouteGenerator.h"
#include "BotGeometry.h"
#include "RouteLine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <set>

namespace Bots
{
    namespace
    {
        std::string Format(Position const& p)
        {
            char text[96];
            std::snprintf(text, sizeof(text), "%.2f %.2f %.2f", p.x, p.y, p.z);
            return text;
        }

        enum class EventKind
        {
            Point, // A point along the center line or a detour.
            Pull,  // The pull point of a pack.
            Pack,
            Visit,
        };

        // A step of the route before it has its places and its name.
        struct Event
        {
            float s;   // The distance along the center line.
            int order; // At the same s: points, pulls and packs, detours, visits.
            int sub;   // The order inside a detour.
            EventKind kind;
            Position center;
            float dx = 1.0f; // The flat direction of the line there.
            float dy = 0.0f;
            std::size_t pack = 0; // For Pull and Pack.
            std::uint32_t quest = 0;
            std::string name; // For Visit.
        };

        struct Detour
        {
            std::size_t pack;
            std::uint32_t quest;
        };

        float PathLength(std::vector<Position> const& path)
        {
            float length = 0.0f;
            for (std::size_t i = 1; i < path.size(); ++i)
                length += Distance3d(path[i - 1], path[i]);
            return length;
        }

        // The member of the pack whose spawn point is nearest to `at`.
        Position NearestMember(Position const& at, GeneratorPack const& pack, std::vector<GeneratorSpawn> const& spawns)
        {
            Position best = spawns[pack.members[0]].position;
            for (std::size_t member : pack.members)
                if (Distance3d(at, spawns[member].position) < Distance3d(at, best))
                    best = spawns[member].position;
            return best;
        }

        enum class Sight
        {
            OutOfRange,
            Hidden,     // In range, but no line of sight.
            OtherLevel, // In range and in sight, but far by path.
            Pullable,
        };

        struct PackSight
        {
            Sight sight;
            float path; // The walk to the nearest member, or -1 with no path.
        };

        // Whether a group at `at` can pull the pack: the pack is within
        // `range`, sees the spot, and stands on its level. On the same level,
        // the navmesh path to the pack is not much longer than the straight
        // distance. With no path at all, for example from a spawn just off
        // the navmesh, the height tells the level.
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
                return {std::fabs(at.z - target.z) <= settings.levelPathSlack ? Sight::Pullable : Sight::OtherLevel,
                    -1.0f};
            float const length = PathLength(path);
            if (length > settings.levelPathFactor * Distance3d(at, target) + settings.levelPathSlack)
                return {Sight::OtherLevel, length};
            return {Sight::Pullable, length};
        }

        // The pull point on a path to a pack: the first spot whose rest of the
        // path is within the limit and that is within aggro range plus the
        // margin of the pack. Without one, the first spot within the limit.
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

        std::string UniqueName(std::set<std::string>& used, std::string const& name)
        {
            std::string candidate = name;
            for (unsigned number = 2; used.count(candidate); ++number)
                candidate = name + "_" + std::to_string(number);
            used.insert(candidate);
            return candidate;
        }
    }

    float RouteRandom::Uniform(float low, float high)
    {
        // The raw output of mt19937 is the same on each library, which the
        // distributions of <random> are not.
        double const unit = double(m_engine() >> 8) / double(1u << 24);
        return float(low + (high - low) * unit);
    }

    std::vector<GeneratorPack> BuildPacks(std::vector<GeneratorSpawn> const& spawns, float clusterDistance)
    {
        std::vector<std::size_t> root(spawns.size());
        std::iota(root.begin(), root.end(), 0);
        std::function<std::size_t(std::size_t)> find = [&](std::size_t i) {
            return root[i] == i ? i : root[i] = find(root[i]);
        };
        auto join = [&](std::size_t a, std::size_t b) {
            std::size_t const ra = find(a);
            std::size_t const rb = find(b);
            if (ra != rb)
                root[std::max(ra, rb)] = std::min(ra, rb);
        };

        std::map<std::uint32_t, std::size_t> byGuid;
        for (std::size_t i = 0; i < spawns.size(); ++i)
            if (spawns[i].hostile)
                byGuid[spawns[i].guid] = i;
        std::set<std::uint32_t> leaders;
        for (std::size_t i = 0; i < spawns.size(); ++i)
        {
            if (!spawns[i].hostile)
                continue;
            auto leader = byGuid.find(spawns[i].groupLeader);
            if (spawns[i].groupLeader && leader != byGuid.end())
            {
                join(i, leader->second);
                leaders.insert(spawns[i].groupLeader);
            }
            for (std::size_t j = 0; j < i; ++j)
                if (spawns[j].hostile && Distance3d(spawns[i].position, spawns[j].position) <= clusterDistance)
                    join(i, j);
        }

        std::vector<GeneratorPack> packs;
        std::map<std::size_t, std::size_t> packOfRoot;
        for (std::size_t i = 0; i < spawns.size(); ++i)
        {
            if (!spawns[i].hostile)
                continue;
            std::size_t const r = find(i);
            auto it = packOfRoot.find(r);
            if (it == packOfRoot.end())
            {
                it = packOfRoot.emplace(r, packs.size()).first;
                packs.emplace_back();
            }
            packs[it->second].members.push_back(i);
        }
        for (GeneratorPack& pack : packs)
        {
            auto leader = std::find_if(pack.members.begin(), pack.members.end(),
                [&](std::size_t i) { return leaders.count(spawns[i].guid) != 0; });
            if (leader != pack.members.end())
                std::rotate(pack.members.begin(), leader, leader + 1);
        }
        return packs;
    }

    float DistanceToPack(Position const& position, GeneratorPack const& pack,
        std::vector<GeneratorSpawn> const& spawns)
    {
        float best = -1.0f;
        for (std::size_t member : pack.members)
        {
            GeneratorSpawn const& spawn = spawns[member];
            float distance = std::max(0.0f, Distance3d(position, spawn.position) - spawn.wanderDistance);
            Position previous = spawn.position;
            for (Position const& waypoint : spawn.patrol)
            {
                distance = std::min(distance, DistanceToSegment(position, previous, waypoint));
                previous = waypoint;
            }
            if (best < 0.0f || distance < best)
                best = distance;
        }
        return best < 0.0f ? 1e9f : best;
    }

    float FirstApproach(std::vector<Position> const& points, GeneratorPack const& pack,
        std::vector<GeneratorSpawn> const& spawns, float range)
    {
        RouteLine const line(points);
        for (float s : line.Samples())
            if (DistanceToPack(line.At(s), pack, spawns) <= range)
                return s;
        return -1.0f;
    }

    float SidewaysOffset(float leftRoom, float rightRoom, float limit, RouteRandom& random)
    {
        float const low = -std::min(std::max(0.0f, rightRoom), limit);
        float const high = std::min(std::max(0.0f, leftRoom), limit);
        float const offset = random.Uniform(low, high);
        return high > low ? offset : 0.0f;
    }

    std::vector<float> AlternativeOffsets(float leftRoom, float rightRoom, float firstOffset,
        GeneratorSettings const& settings, RouteRandom& random)
    {
        std::vector<float> offsets;
        if (leftRoom + rightRoom < settings.wideRoom)
            return offsets;
        float const left = std::min(leftRoom, settings.alternativeLimit);
        float const right = -std::min(rightRoom, settings.alternativeLimit);
        if (left >= firstOffset + settings.alternativeGap)
            offsets.push_back(random.Uniform(firstOffset + settings.alternativeGap, left));
        if (right <= firstOffset - settings.alternativeGap)
            offsets.push_back(random.Uniform(right, firstOffset - settings.alternativeGap));
        return offsets;
    }

    PackFill FillCount(std::vector<GeneratorPack> const& packs, std::vector<bool> const& onRoute,
        std::vector<float> const& distanceToRoute, std::vector<GeneratorSpawn> const& spawns,
        std::vector<std::uint32_t> const& entries, std::uint32_t count)
    {
        auto countIn = [&](GeneratorPack const& pack) {
            std::uint32_t n = 0;
            for (std::size_t member : pack.members)
                if (std::find(entries.begin(), entries.end(), spawns[member].entry) != entries.end())
                    ++n;
            return n;
        };

        PackFill fill{{}, 0};
        std::vector<std::size_t> candidates;
        for (std::size_t i = 0; i < packs.size(); ++i)
        {
            std::uint32_t const n = countIn(packs[i]);
            if (onRoute[i])
                fill.have += n;
            else if (n)
                candidates.push_back(i);
        }
        std::stable_sort(candidates.begin(), candidates.end(),
            [&](std::size_t a, std::size_t b) { return distanceToRoute[a] < distanceToRoute[b]; });
        for (std::size_t candidate : candidates)
        {
            if (fill.have >= count)
                break;
            fill.added.push_back(candidate);
            fill.have += countIn(packs[candidate]);
        }
        return fill;
    }

    std::string StepNameOf(std::string const& text)
    {
        std::string name;
        for (char c : text)
        {
            if (c >= 'A' && c <= 'Z')
                name += char(c - 'A' + 'a');
            else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
                name += c;
            else if ((c == ' ' || c == '_' || c == '-') && !name.empty() && name.back() != '_')
                name += '_';
        }
        while (!name.empty() && name.back() == '_')
            name.pop_back();
        return name;
    }

    GeneratedRoute GenerateRoute(GeneratorInput const& input, RouteNav& nav)
    {
        GeneratorSettings const& settings = input.settings;
        std::vector<GeneratorSpawn> const& spawns = input.spawns;
        GeneratedRoute result;
        float const range = settings.aggroRange + settings.margin;

        // The center line: the entrance, then each boss.
        std::vector<Position> centerPoints{input.entrance};
        std::vector<std::size_t> bossSpawns;
        for (std::uint32_t boss : input.bosses)
        {
            auto spawn = std::find_if(spawns.begin(), spawns.end(),
                [boss](GeneratorSpawn const& s) { return s.entry == boss; });
            if (spawn == spawns.end())
            {
                result.error = "the map has no spawn of the boss " + std::to_string(boss);
                return result;
            }
            bossSpawns.push_back(std::size_t(spawn - spawns.begin()));
            std::vector<Position> leg;
            if (!nav.Path(centerPoints.back(), spawn->position, leg) || leg.empty())
            {
                result.error = "no navmesh path to the boss " + std::to_string(boss) + " at " +
                    Format(spawn->position) + " from " + Format(centerPoints.back());
                return result;
            }
            centerPoints.insert(centerPoints.end(), leg.begin() + 1, leg.end());
            if (leg.size() == 1)
                centerPoints.push_back(leg[0]);
        }
        RouteLine const line(centerPoints);
        std::vector<float> const samples = line.Samples();

        // The packs, and where the line first comes near each one.
        std::vector<GeneratorPack> const packs = BuildPacks(spawns, settings.clusterDistance);
        std::vector<float> approach(packs.size(), -1.0f);
        std::vector<bool> onRoute(packs.size(), false);
        std::vector<float> distanceToRoute(packs.size(), 1e9f);
        std::vector<float> nearestS(packs.size(), 0.0f);
        std::vector<std::uint32_t> questOf(packs.size(), 0);
        std::vector<std::uint32_t> bossOf(packs.size(), 0);
        for (std::size_t p = 0; p < packs.size(); ++p)
        {
            for (float s : samples)
            {
                float const distance = DistanceToPack(line.At(s), packs[p], spawns);
                if (distance < distanceToRoute[p])
                {
                    distanceToRoute[p] = distance;
                    nearestS[p] = s;
                }
            }
            for (std::size_t b = 0; b < bossSpawns.size(); ++b)
                if (std::find(packs[p].members.begin(), packs[p].members.end(), bossSpawns[b]) !=
                    packs[p].members.end())
                    bossOf[p] = input.bosses[b];
        }

        // A pack is on the route when a group that walks the line can pull
        // it: from a spot of the line in range, the pack sees the group and
        // stands on its level. So a pack on another level of a cave is on
        // the route only where the line itself goes to that level. The pull
        // point is the first such spot whose navmesh path to the pack is
        // within the limit, else the one with the shortest path.
        std::vector<float> pullAt(packs.size(), -1.0f);
        std::vector<float> pullPath(packs.size(), -1.0f); // -1: no path.
        struct LeftOut
        {
            std::size_t pack;
            bool seen;          // Some spot in range sees it: on another level.
            float shortestPath; // From a spot that sees it, or -1 with no path.
            float distance;     // The straight distance to the line.
        };
        std::vector<LeftOut> leftOut;
        for (std::size_t p = 0; p < packs.size(); ++p)
        {
            if (distanceToRoute[p] > range)
                continue;
            bool seen = false;
            float otherPath = -1.0f;
            for (float s : samples)
            {
                PackSight const sight = SightOf(line.At(s), packs[p], spawns, settings, nav, range);
                if (sight.sight == Sight::OutOfRange || sight.sight == Sight::Hidden)
                    continue;
                seen = true;
                if (sight.sight == Sight::OtherLevel)
                {
                    if (otherPath < 0.0f || sight.path < otherPath)
                        otherPath = sight.path;
                    continue;
                }
                if (approach[p] < 0.0f)
                    approach[p] = pullAt[p] = s;
                if (sight.path < 0.0f)
                {
                    if (pullPath[p] < 0.0f)
                        break;
                    continue;
                }
                if (pullPath[p] < 0.0f || sight.path < pullPath[p])
                {
                    pullPath[p] = sight.path;
                    pullAt[p] = s;
                }
                if (sight.path <= settings.pullPathLimit)
                    break;
            }
            onRoute[p] = approach[p] >= 0.0f;
            if (!onRoute[p])
                leftOut.push_back({p, seen, otherPath, distanceToRoute[p]});
        }

        // A pack off the route that a quest may need is as far from the
        // route as the shortest navmesh path from a spot of the line: a
        // quest takes the nearest packs, and its detour starts at that spot.
        // So a quest pack on another level is reached on its own level.
        std::set<std::uint32_t> questEntries;
        for (GeneratorQuest const& quest : input.quests)
            for (QuestObjective const& objective : quest.objectives)
            {
                if (objective.kind == QuestObjectiveKind::Kill)
                    questEntries.insert(objective.entry);
                questEntries.insert(objective.entries.begin(), objective.entries.end());
            }
        for (std::size_t p = 0; p < packs.size(); ++p)
        {
            if (onRoute[p])
                continue;
            bool needed = false;
            for (std::size_t member : packs[p].members)
                needed = needed || questEntries.count(spawns[member].entry) != 0;
            for (GeneratorQuest const& quest : input.quests)
                for (GeneratorVisit const& visit : quest.visits)
                    needed = needed || DistanceToPack(visit.position, packs[p], spawns) < settings.aggroRange;
            if (!needed)
                continue;
            Position const target = spawns[packs[p].members[0]].position;
            std::vector<std::pair<float, float>> nearest; // The straight distance, then s.
            for (float s : samples)
                nearest.push_back({Distance3d(line.At(s), target), s});
            std::sort(nearest.begin(), nearest.end());
            float best = -1.0f;
            for (auto const& candidate : nearest)
            {
                // A path is never shorter than the straight distance.
                if (best >= 0.0f && candidate.first >= best)
                    break;
                std::vector<Position> path;
                if (!nav.Path(line.At(candidate.second), target, path))
                    continue;
                float const length = PathLength(path);
                if (best < 0.0f || length < best)
                {
                    best = length;
                    nearestS[p] = candidate.second;
                }
            }
            if (best >= 0.0f)
                distanceToRoute[p] = best;
        }

        std::vector<Detour> detours;
        std::vector<Event> events;
        std::set<std::string> visitNames;

        // The quests: visit points, and packs for the kill counts and items.
        for (GeneratorQuest const& quest : input.quests)
        {
            RouteQuest routeQuest{quest.id, {}};
            for (GeneratorVisit const& visit : quest.visits)
            {
                std::string const name = "q" + std::to_string(quest.id) + "_" + StepNameOf(visit.name);
                // Two quests at one corpse share the point of the first quest.
                std::string pointName;
                for (Event const& event : events)
                    if (event.kind == EventKind::Visit && Distance3d(event.center, visit.position) < 0.5f)
                        pointName = event.name;
                if (pointName.empty())
                {
                    float bestS = 0.0f;
                    float best = 1e9f;
                    for (float s : samples)
                        if (Distance3d(line.At(s), visit.position) < best)
                        {
                            best = Distance3d(line.At(s), visit.position);
                            bestS = s;
                        }
                    // The packs that guard the spot come first. A guard off the
                    // line becomes a pack of the quest.
                    for (std::size_t p = 0; p < packs.size(); ++p)
                    {
                        if (DistanceToPack(visit.position, packs[p], spawns) >= settings.aggroRange ||
                            SightOf(visit.position, packs[p], spawns, settings, nav, settings.aggroRange).sight !=
                                Sight::Pullable)
                            continue;
                        if (!onRoute[p])
                        {
                            onRoute[p] = true;
                            questOf[p] = quest.id;
                            detours.push_back({p, quest.id});
                            bestS = std::max(bestS, nearestS[p]);
                        }
                        else if (approach[p] >= 0.0f)
                            bestS = std::max(bestS, pullAt[p]);
                    }
                    Event event{bestS, 5, 0, EventKind::Visit, visit.position};
                    event.quest = quest.id;
                    event.name = pointName = UniqueName(visitNames, name);
                    events.push_back(event);
                }
                QuestObjective objective{QuestObjectiveKind::Visit};
                objective.point = pointName;
                routeQuest.objectives.push_back(objective);
                result.objectives.push_back({quest.id, objective, 1});
            }
            for (QuestObjective const& objective : quest.objectives)
            {
                std::vector<std::uint32_t> entries;
                std::uint32_t need = 1; // An item needs a dropper on the route.
                if (objective.kind == QuestObjectiveKind::Kill)
                {
                    entries.push_back(objective.entry);
                    need = objective.count;
                }
                else if (objective.kind == QuestObjectiveKind::Item)
                    entries = objective.entries;
                else
                    continue;
                PackFill const fill = FillCount(packs, onRoute, distanceToRoute, spawns, entries, need);
                for (std::size_t p : fill.added)
                {
                    onRoute[p] = true;
                    questOf[p] = quest.id;
                    detours.push_back({p, quest.id});
                }
                routeQuest.objectives.push_back(objective);
                result.objectives.push_back({quest.id, objective, fill.have});
                if (fill.have < need)
                    result.warnings.push_back("quest " + std::to_string(quest.id) + ": the dungeon has only " +
                        std::to_string(fill.have) + " of " + std::to_string(need) + " spawns for an objective");
            }
            result.route.quests.push_back(routeQuest);
        }

        // The packs of the line, with their pull points.
        for (std::size_t p = 0; p < packs.size(); ++p)
        {
            if (approach[p] < 0.0f)
                continue;
            float dx, dy;
            line.Direction(pullAt[p], dx, dy);
            // Two packs that the line reaches at one spot: each pull point
            // comes right before its own pack.
            Event pull{pullAt[p], 1, int(2 * p), EventKind::Pull, line.At(pullAt[p])};
            pull.dx = dx;
            pull.dy = dy;
            pull.pack = p;
            events.push_back(pull);
            Event fight{pullAt[p], 1, int(2 * p + 1), EventKind::Pack, spawns[packs[p].members[0]].position};
            fight.pack = p;
            events.push_back(fight);
        }

        // The detours to the packs off the line, in route order. A detour
        // also fights each pack off the route, or of a later detour, that it
        // can pull on its way.
        std::stable_sort(detours.begin(), detours.end(),
            [&](Detour const& a, Detour const& b) { return nearestS[a.pack] < nearestS[b.pack]; });
        std::vector<bool> detourTarget(packs.size(), false);
        for (Detour const& detour : detours)
            detourTarget[detour.pack] = true;
        std::vector<bool> foughtOnDetour(packs.size(), false);
        for (std::size_t d = 0; d < detours.size(); ++d)
        {
            Detour const& detour = detours[d];
            std::size_t const p = detour.pack;
            if (foughtOnDetour[p])
                continue;
            Position const start = line.At(nearestS[p]);
            Position const target = spawns[packs[p].members[0]].position;
            std::vector<Position> path;
            if (!nav.Path(start, target, path) || path.size() < 1)
            {
                result.warnings.push_back("quest " + std::to_string(detour.quest) + ": no navmesh path to the pack of " +
                    std::to_string(spawns[packs[p].members[0]].guid) + " at " + Format(target));
                onRoute[p] = false;
                continue;
            }
            RouteLine const way(path);
            std::vector<std::pair<float, std::size_t>> fights{{PullOnPath(way, packs[p], spawns, settings), p}};
            foughtOnDetour[p] = true;
            for (std::size_t q = 0; q < packs.size(); ++q)
            {
                if (foughtOnDetour[q] || (onRoute[q] && !detourTarget[q]))
                    continue;
                float at = -1.0f;
                for (float s : way.Samples())
                    if (SightOf(way.At(s), packs[q], spawns, settings, nav, range).sight == Sight::Pullable)
                    {
                        at = s;
                        break;
                    }
                if (at < 0.0f)
                    continue;
                fights.push_back({at, q});
                foughtOnDetour[q] = true;
                onRoute[q] = true;
            }
            std::stable_sort(fights.begin(), fights.end(),
                [](std::pair<float, std::size_t> const& a, std::pair<float, std::size_t> const& b) {
                    return a.first < b.first;
                });

            // Two detours from one spot keep their steps apart.
            int sub = int(d) * 100000;
            std::size_t next = 0;
            float const lastPull = fights.back().first;
            auto addFights = [&](float upTo) {
                for (; next < fights.size() && fights[next].first <= upTo; ++next)
                {
                    float const at = fights[next].first;
                    std::size_t const q = fights[next].second;
                    Event pull{nearestS[p], 4, sub++, EventKind::Pull, way.At(at)};
                    way.Direction(at, pull.dx, pull.dy);
                    pull.pack = q;
                    pull.quest = detour.quest;
                    events.push_back(pull);
                    Event fight{nearestS[p], 4, sub++, EventKind::Pack, spawns[packs[q].members[0]].position};
                    fight.pack = q;
                    fight.quest = detour.quest;
                    events.push_back(fight);
                }
            };
            for (float at = settings.pointSpacing; at < lastPull - settings.pointSpacing / 2.0f;
                 at += settings.pointSpacing)
            {
                addFights(at);
                bool nearPull = false;
                for (auto const& fight : fights)
                    nearPull = nearPull || std::fabs(fight.first - at) < settings.pointSpacing / 2.0f;
                if (nearPull)
                    continue;
                Event point{nearestS[p], 4, sub++, EventKind::Point, way.At(at)};
                way.Direction(at, point.dx, point.dy);
                point.quest = detour.quest;
                events.push_back(point);
            }
            addFights(way.Length() + 1.0f);
        }

        // The packs in range of the line that it cannot pull.
        for (LeftOut const& out : leftOut)
        {
            GeneratorSpawn const& leader = spawns[packs[out.pack].members[0]];
            char head[120];
            std::snprintf(head, sizeof(head), "the pack of spawn %u at %s is off the route: ",
                unsigned(leader.guid), Format(leader.position).c_str());
            char reason[200];
            if (!out.seen)
                std::snprintf(reason, sizeof(reason),
                    "no line of sight from the line, which passes it at %.0f yards", out.distance);
            else if (out.shortestPath < 0.0f)
                std::snprintf(reason, sizeof(reason),
                    "it is on another level (the line passes it at %.0f yards, with no navmesh path to it)",
                    out.distance);
            else
                std::snprintf(reason, sizeof(reason),
                    "it is on another level (the line passes it at %.0f yards, but the shortest path from the line "
                    "to it is %.0f yards)",
                    out.distance, out.shortestPath);
            std::string warning = std::string(head) + reason;
            if (onRoute[out.pack])
                warning += questOf[out.pack] ? "; a detour for quest " + std::to_string(questOf[out.pack]) +
                        " fights it" : "; a detour fights it";
            result.warnings.push_back(warning);
        }

        // The points along the line, away from the pull and visit points.
        for (float s = 0.0f; s <= line.Length(); s += settings.pointSpacing)
        {
            bool nearOther = false;
            for (Event const& event : events)
                if ((event.kind == EventKind::Pull && event.order == 1) || event.kind == EventKind::Visit)
                    nearOther = nearOther || std::fabs(event.s - s) < settings.pointSpacing / 2.0f;
            if (nearOther)
                continue;
            Event point{s, 0, 0, EventKind::Point, line.At(s)};
            line.Direction(s, point.dx, point.dy);
            events.push_back(point);
        }

        std::stable_sort(events.begin(), events.end(), [](Event const& a, Event const& b) {
            if (a.s != b.s)
                return a.s < b.s;
            if (a.order != b.order)
                return a.order < b.order;
            return a.sub < b.sub;
        });

        // The order of the fights, for the aggro check of each place.
        std::vector<std::size_t> foughtAt(packs.size(), events.size());
        for (std::size_t e = 0; e < events.size(); ++e)
            if (events[e].kind == EventKind::Pack)
                foughtAt[events[e].pack] = e;

        // The names of the packs, in route order: bosses by name.
        Route& route = result.route;
        route.name = input.name;
        route.mapId = input.mapId;
        route.hasSeed = true;
        route.seed = settings.seed;
        route.bosses = input.bosses;
        std::vector<std::string> packName(packs.size());
        {
            std::set<std::string> used = visitNames;
            unsigned number = 0;
            for (Event const& event : events)
            {
                if (event.kind != EventKind::Pack)
                    continue;
                std::size_t const p = event.pack;
                std::string const boss = bossOf[p] && input.entryName ? StepNameOf(input.entryName(bossOf[p])) : "";
                if (!boss.empty() && !used.count(boss))
                    packName[p] = boss;
                else
                {
                    do
                        packName[p] = "pack" + std::to_string(++number);
                    while (used.count(packName[p]));
                }
                used.insert(packName[p]);
            }
        }

        // A place must not pull a pack that the route fights later, nor a
        // pack off the route. A pull point keeps outside its own pack too.
        // A pack that cannot see the place, or is on another level, is no
        // danger. The place of a pull point of the line keeps the path to
        // its pack within the limit.
        auto allowed = [&](Position const& place, std::size_t e) {
            for (std::size_t p = 0; p < packs.size(); ++p)
                if (foughtAt[p] > e && DistanceToPack(place, packs[p], spawns) < settings.aggroRange &&
                    SightOf(place, packs[p], spawns, settings, nav, settings.aggroRange).sight == Sight::Pullable)
                    return false;
            Event const& event = events[e];
            if (event.kind != EventKind::Pull || event.order != 1 || pullPath[event.pack] < 0.0f ||
                Distance3d(place, event.center) < 1e-3f)
                return true;
            std::vector<Position> path;
            return nav.Path(place, NearestMember(place, packs[event.pack], spawns), path) &&
                PathLength(path) <= std::max(settings.pullPathLimit, pullPath[event.pack]);
        };

        RouteRandom random(settings.seed);
        for (std::size_t e = 0; e < events.size(); ++e)
        {
            Event const& event = events[e];
            if (event.kind == EventKind::Pack)
            {
                std::vector<RouteMob> mobs;
                for (std::size_t member : packs[event.pack].members)
                    mobs.push_back({spawns[member].guid, spawns[member].entry});
                AddPack(route, packName[event.pack], event.center, mobs);
                route.packs.back().quest = event.quest;
                continue;
            }
            if (event.kind == EventKind::Visit)
            {
                AddPoint(route, event.name, {event.center, settings.pointRadius});
                route.points.back().quest = event.quest;
                continue;
            }

            // A point or pull point: off the line by a random distance.
            float const nx = -event.dy;
            float const ny = event.dx;
            float const probe = std::max(settings.alternativeLimit, settings.sideLimit) + settings.wallClearance;
            float const leftRoom = std::max(0.0f,
                Distance2d(event.center, nav.Reach(event.center, nx, ny, probe)) - settings.wallClearance);
            float const rightRoom = std::max(0.0f,
                Distance2d(event.center, nav.Reach(event.center, -nx, -ny, probe)) - settings.wallClearance);
            auto placeAt = [&](float offset) {
                if (std::fabs(offset) < 1e-3f)
                    return event.center;
                float const sign = offset > 0.0f ? 1.0f : -1.0f;
                return nav.Reach(event.center, nx * sign, ny * sign, std::fabs(offset));
            };

            float offset = SidewaysOffset(leftRoom, rightRoom, settings.sideLimit, random);
            Position first = placeAt(offset);
            for (int retry = 0; retry < 3 && !allowed(first, e); ++retry)
            {
                offset /= 2.0f;
                first = placeAt(offset);
            }
            if (!allowed(first, e))
            {
                offset = 0.0f;
                first = event.center;
                if (!allowed(first, e))
                    result.warnings.push_back("a point at " + Format(first) + " is within aggro range of a pack");
            }

            std::string const name = event.kind == EventKind::Pull ? "pull_" + packName[event.pack] : "";
            AddPoint(route, name, {first, settings.pointRadius});
            route.points.back().quest = event.quest;
            for (float alternative : AlternativeOffsets(leftRoom, rightRoom, offset, settings, random))
            {
                Position const place = placeAt(alternative);
                if (allowed(place, e) && Distance2d(place, first) >= settings.alternativeGap * 0.5f)
                    AddAlternative(route, {place, settings.pointRadius});
            }
            if (event.kind == EventKind::Pull &&
                DistanceToPack(first, packs[event.pack], spawns) < settings.aggroRange)
                result.warnings.push_back("the pull point of " + packName[event.pack] + " is within aggro range");
            std::size_t const p = event.pack;
            if (event.kind == EventKind::Pull && event.order == 1 && pullPath[p] > settings.pullPathLimit)
            {
                char text[200];
                std::snprintf(text, sizeof(text),
                    "%s has no pull point within %.0f yards by path on the line: the shortest is %.0f yards, at %s",
                    packName[p].c_str(), settings.pullPathLimit, pullPath[p], Format(event.center).c_str());
                result.warnings.push_back(text);
            }
        }

        // Each alternative has its own path to the next point.
        RoutePoint* previous = nullptr;
        for (RouteStep const& step : route.steps)
        {
            if (step.kind != RouteStepKind::Point)
                continue;
            RoutePoint& point = route.points[step.index];
            if (previous)
            {
                std::vector<Position> path;
                std::vector<RoutePlace> kept{previous->places[0]};
                for (std::size_t k = 1; k < previous->places.size(); ++k)
                    if (nav.Path(previous->places[k].position, point.places[0].position, path))
                        kept.push_back(previous->places[k]);
                previous->places = kept;
            }
            previous = &point;
        }

        result.ok = true;
        return result;
    }

    std::vector<std::string> CheckRouteWalk(Route const& route, RouteNav& nav)
    {
        std::vector<std::string> problems;
        std::vector<RoutePoint const*> points;
        for (RouteStep const& step : route.steps)
            if (step.kind == RouteStepKind::Point)
                points.push_back(&route.points[step.index]);

        for (RoutePoint const* point : points)
            for (std::size_t k = 0; k < point->places.size(); ++k)
                if (!nav.OnMesh(point->places[k].position))
                    problems.push_back("point " + point->name + ": the place " + std::to_string(k + 1) + " at " +
                        Format(point->places[k].position) + " is not on the navmesh");

        for (std::size_t i = 0; i + 1 < points.size(); ++i)
        {
            RoutePoint const& point = *points[i];
            RoutePoint const& next = *points[i + 1];
            for (std::size_t k = 0; k < point.places.size(); ++k)
            {
                std::vector<Position> path;
                if (!nav.Path(point.places[k].position, next.places[0].position, path))
                    problems.push_back("point " + point.name + ": no path from the place " + std::to_string(k + 1) +
                        " to the point " + next.name);
            }
        }
        return problems;
    }
}
