#include "BotGeometry.h"
#include "RouteHandOrder.h"
#include "RouteLine.h"
#include "RouteTrace.h"

#include <algorithm>
#include <cstdio>
#include <set>

// `.route build`: the route around a GM trace (RouteTrace.h).
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

        // The center line: parts of joined points, one after the other. The
        // distance along it skips one yard between two parts, so that no
        // position of the line lies on a gap.
        class TraceLine
        {
        public:
            explicit TraceLine(std::vector<TracePoint> const& points)
            {
                for (std::size_t i = 0; i < points.size(); ++i)
                {
                    if (i == 0 || !points[i].joined)
                        m_parts.push_back({0.0f, RouteLine({points[i].position}), {}, {}});
                    m_parts.back().positions.push_back(points[i].position);
                    m_parts.back().indices.push_back(i);
                }
                for (std::size_t p = 0; p < m_parts.size(); ++p)
                {
                    m_parts[p].line = RouteLine(m_parts[p].positions);
                    if (p > 0)
                        m_parts[p].start = End(p - 1) + 1.0f;
                }
            }

            std::size_t Parts() const { return m_parts.size(); }
            float Start(std::size_t part) const { return m_parts[part].start; }
            float End(std::size_t part) const { return m_parts[part].start + m_parts[part].line.Length(); }
            float Length() const { return m_parts.empty() ? 0.0f : End(m_parts.size() - 1); }

            // The distance along the line of trace point i.
            float ArcOfPoint(std::size_t i) const
            {
                for (Part const& part : m_parts)
                    for (std::size_t k = 0; k < part.indices.size(); ++k)
                        if (part.indices[k] == i)
                            return part.start + part.line.ArcOf(k);
                return 0.0f;
            }

            Position At(float s) const
            {
                for (std::size_t p = m_parts.size(); p-- > 0;)
                    if (s >= m_parts[p].start || p == 0)
                        return m_parts[p].line.At(std::min(s, End(p)) - m_parts[p].start);
                return {};
            }

            // The distances at 1 yard steps along each part, and its end.
            std::vector<float> Samples() const
            {
                std::vector<float> samples;
                for (Part const& part : m_parts)
                    for (float s : part.line.Samples())
                        samples.push_back(part.start + s);
                return samples;
            }

        private:
            struct Part
            {
                float start;
                RouteLine line;
                std::vector<Position> positions;
                std::vector<std::size_t> indices;
            };
            std::vector<Part> m_parts;
        };

        // The walks of the trace as one line. A later walk that starts within
        // `join` of the line is walked out and back from the nearest spot of
        // the line; any other later walk follows the end.
        std::vector<TracePoint> CenterLine(Trace const& trace, float join, std::vector<std::string>& warnings)
        {
            std::vector<std::vector<TracePoint>> walks;
            for (TracePoint const& point : trace.points)
            {
                if (walks.empty() || point.walk)
                    walks.emplace_back();
                walks.back().push_back(point);
            }
            std::vector<TracePoint> line;
            for (std::size_t w = 0; w < walks.size(); ++w)
            {
                std::vector<TracePoint> walk = walks[w];
                walk.front().walk = false;
                if (line.empty())
                {
                    walk.front().joined = false;
                    line = walk;
                    continue;
                }

                // The nearest spot of the line to the start of the walk.
                Position const start = walk.front().position;
                float best = 1e9f;
                std::size_t at = 0;
                Position spot{};
                for (std::size_t i = 0; i < line.size(); ++i)
                {
                    Position candidate = line[i].position;
                    if (i > 0 && line[i].joined)
                    {
                        Position const& a = line[i - 1].position;
                        Position const& b = line[i].position;
                        float const vx = b.x - a.x, vy = b.y - a.y, vz = b.z - a.z;
                        float const length2 = vx * vx + vy * vy + vz * vz;
                        float t = length2 > 0.0f ? ((start.x - a.x) * vx + (start.y - a.y) * vy + (start.z - a.z) * vz) / length2 : 0.0f;
                        t = std::max(0.0f, std::min(1.0f, t));
                        candidate = {a.x + vx * t, a.y + vy * t, a.z + vz * t};
                    }
                    if (Distance3d(candidate, start) < best)
                    {
                        best = Distance3d(candidate, start);
                        at = i;
                        spot = candidate;
                    }
                }
                if (best > join)
                {
                    warnings.push_back("the walk " + std::to_string(w + 1) + " starts " + std::to_string(int(best)) +
                        " yards from the trace: the route goes there after the end");
                    walk.front().joined = false;
                    line.insert(line.end(), walk.begin(), walk.end());
                    continue;
                }

                // Out along the walk, and back the same way.
                std::vector<TracePoint> branch;
                TracePoint split;
                split.position = spot;
                split.joined = at > 0 && line[at].joined;
                branch.push_back(split);
                walk.front().joined = true;
                branch.insert(branch.end(), walk.begin(), walk.end());
                for (std::size_t k = walk.size() - 1; k-- > 0;)
                {
                    TracePoint back = walk[k];
                    back.joined = walk[k + 1].joined;
                    // A checkpoint is one route point, on the way out.
                    back.mark = false;
                    back.note.clear();
                    branch.push_back(back);
                }
                TracePoint rejoin;
                rejoin.position = spot;
                branch.push_back(rejoin);
                if (split.joined)
                {
                    // The spot lies on the segment to point `at`.
                    line.insert(line.begin() + std::ptrdiff_t(at), branch.begin(), branch.end());
                    continue;
                }
                // The spot is point `at`, the start of a part: the branch
                // comes right after it.
                line.insert(line.begin() + std::ptrdiff_t(at + 1), branch.begin() + 1, branch.end());
            }
            return line;
        }

        using Event = RouteEvent;
        using EventKind = RouteEventKind;

        std::string UniqueName(std::set<std::string>& used, std::string const& name)
        {
            std::string candidate = name;
            for (unsigned number = 2; used.count(candidate); ++number)
                candidate = name + "_" + std::to_string(number);
            used.insert(candidate);
            return candidate;
        }
    }

    TraceBuild BuildTraceRoute(TraceBuildInput const& input)
    {
        GeneratorSettings const& settings = input.settings;
        std::vector<GeneratorSpawn> const& spawns = input.spawns;
        TraceBuild result;
        if (input.trace.points.empty())
        {
            result.error = "the trace has no points";
            return result;
        }
        float const range = settings.aggroRange + settings.margin;
        float const spacing = std::max(1.0f, settings.pointSpacing);

        std::vector<TracePoint> const points = CenterLine(input.trace, spacing, result.warnings);
        TraceLine const line(points);
        std::vector<float> const samples = line.Samples();

        // The bots walk straight across a gap from one part to the next.
        for (std::size_t p = 1; p < line.Parts(); ++p)
        {
            Position const from = line.At(line.End(p - 1));
            Position const to = line.At(line.Start(p));
            float const gap = Distance3d(from, to);
            if (gap > spacing)
                result.warnings.push_back("the route goes straight across a gap of " + std::to_string(int(gap + 0.5f)) +
                    " yards from " + Format(from) + " to " + Format(to) +
                    " that the GM did not walk: walk it with .route trace start, or check it");
        }

        // The packs, and where the trace first comes near each one.
        std::vector<GeneratorPack> const packs = BuildPacks(spawns, settings.clusterDistance);
        HandOrderPacks const hand = ResolveHandOrder(input.packOrder, input.skippedPacks, packs, spawns,
            result.warnings);
        if (!hand.order.empty() && !input.nav)
        {
            result.error = "the hand pack order needs the navmesh: give a character on the map of the route";
            return result;
        }
        std::vector<float> approach(packs.size(), -1.0f);
        std::vector<bool> onRoute(packs.size(), false);
        std::vector<float> distanceToRoute(packs.size(), 1e9f);
        std::vector<std::uint32_t> bossOf(packs.size(), 0);
        std::vector<std::size_t> packOfSpawn(spawns.size(), packs.size());
        for (std::size_t p = 0; p < packs.size(); ++p)
        {
            for (std::size_t member : packs[p].members)
                packOfSpawn[member] = p;
            for (float s : samples)
            {
                float const distance = DistanceToPack(line.At(s), packs[p], spawns);
                if (approach[p] < 0.0f && distance <= range && !hand.skipped[p])
                    approach[p] = s;
                distanceToRoute[p] = std::min(distanceToRoute[p], distance);
            }
            onRoute[p] = approach[p] >= 0.0f;
        }

        // The bosses: the trace must pass each one within aggro range.
        std::vector<std::size_t> bossPacks;
        for (std::uint32_t boss : input.bosses)
        {
            auto spawn = std::find_if(spawns.begin(), spawns.end(),
                [boss](GeneratorSpawn const& s) { return s.entry == boss; });
            if (spawn == spawns.end())
            {
                result.error = "the map has no spawn of the boss " + std::to_string(boss);
                return result;
            }
            std::size_t const p = packOfSpawn[std::size_t(spawn - spawns.begin())];
            if (p == packs.size())
            {
                result.error = "the boss " + std::to_string(boss) + " is not hostile";
                return result;
            }
            bossOf[p] = boss;
            if (distanceToRoute[p] > settings.aggroRange)
                result.unmetBosses.push_back(boss);
            else
                bossPacks.push_back(p);
        }
        for (std::size_t b = 1; b < bossPacks.size(); ++b)
            if (approach[bossPacks[b]] < approach[bossPacks[b - 1]])
                result.warnings.push_back("the trace reaches the boss " + std::to_string(bossOf[bossPacks[b]]) +
                    " before the boss " + std::to_string(bossOf[bossPacks[b - 1]]) + " of the boss list");

        std::vector<Event> events;
        std::set<std::string> used;

        // The quests: visit points on the trace, and the kill counts and
        // items that the packs of the trace meet.
        for (GeneratorQuest const& quest : input.quests)
        {
            RouteQuest routeQuest{quest.id, {}};
            for (GeneratorVisit const& visit : quest.visits)
            {
                std::string pointName;
                for (Event const& event : events)
                    if (event.kind == EventKind::Visit && Distance3d(event.spot, visit.position) < 0.5f)
                        pointName = event.name;
                float bestS = 0.0f;
                float best = 1e9f;
                for (float s : samples)
                    if (Distance3d(line.At(s), visit.position) < best)
                    {
                        best = Distance3d(line.At(s), visit.position);
                        bestS = s;
                    }
                if (pointName.empty())
                {
                    Event event{bestS, 2, 0, EventKind::Visit, line.At(bestS)};
                    event.quest = quest.id;
                    event.spot = visit.position;
                    event.name = pointName =
                        UniqueName(used, "q" + std::to_string(quest.id) + "_" + StepNameOf(visit.name));
                    events.push_back(event);
                }
                QuestObjective objective{QuestObjectiveKind::Visit};
                objective.point = pointName;
                routeQuest.objectives.push_back(objective);
                bool const met = best <= input.visitRange;
                result.objectives.push_back({quest.id, objective, met ? 1u : 0u});
                if (!met)
                {
                    UnmetObjective unmet{quest.id, objective, 0, 1, {}};
                    unmet.distance = best;
                    result.unmet.push_back(unmet);
                }
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
                std::uint32_t const have = FillCount(packs, onRoute, distanceToRoute, spawns, entries, 0, hand.skipped).have;
                routeQuest.objectives.push_back(objective);
                result.objectives.push_back({quest.id, objective, have});
                if (have >= need)
                    continue;
                UnmetObjective unmet{quest.id, objective, have, need, {}};
                for (std::size_t p : FillCount(packs, onRoute, distanceToRoute, spawns, entries, need, hand.skipped).added)
                {
                    NearPack near{{}, spawns[packs[p].members[0]].position, 0, distanceToRoute[p]};
                    for (std::size_t member : packs[p].members)
                    {
                        near.mobs.push_back({spawns[member].guid, spawns[member].entry});
                        if (std::find(entries.begin(), entries.end(), spawns[member].entry) != entries.end())
                            ++near.count;
                    }
                    unmet.nearest.push_back(near);
                }
                result.unmet.push_back(unmet);
            }
            result.route.quests.push_back(routeQuest);
        }

        // The packs of the trace, each with its pull point before it.
        for (std::size_t p = 0; p < packs.size(); ++p)
        {
            if (approach[p] < 0.0f)
                continue;
            Event pull{approach[p], 1, int(2 * p), EventKind::Pull, line.At(approach[p])};
            pull.pack = p;
            events.push_back(pull);
            Event fight{approach[p], 1, int(2 * p + 1), EventKind::Pack, spawns[packs[p].members[0]].position};
            fight.pack = p;
            events.push_back(fight);
        }

        // The checkpoints, and the points between them at the spacing. The
        // spacing counts again from each checkpoint and each part.
        std::vector<Event> marks;
        for (std::size_t i = 0; i < points.size(); ++i)
            if (points[i].mark)
                marks.push_back({line.ArcOfPoint(i), 0, 0, EventKind::Mark, points[i].position});
        std::vector<Event> regular;
        for (std::size_t part = 0; part < line.Parts(); ++part)
        {
            float const end = line.End(part);
            std::vector<std::pair<float, bool>> starts{{line.Start(part), false}}; // (s, is a mark)
            for (Event const& mark : marks)
                if (mark.s >= line.Start(part) && mark.s <= end)
                    starts.push_back({mark.s, true});
            std::stable_sort(starts.begin(), starts.end(),
                [](std::pair<float, bool> const& a, std::pair<float, bool> const& b) { return a.first < b.first; });
            float last = -1e9f;
            for (std::size_t k = 0; k < starts.size(); ++k)
            {
                float const from = starts[k].first;
                bool const nextIsMark = k + 1 < starts.size();
                float const to = nextIsMark ? starts[k + 1].first : end;
                if (starts[k].second)
                    last = from;
                for (float s = starts[k].second ? from + spacing : from; s <= to + 1e-3f; s += spacing)
                {
                    if (nextIsMark && s > to - spacing / 2.0f)
                        break;
                    regular.push_back({std::min(s, end), 0, 0, EventKind::Point, line.At(std::min(s, end))});
                    last = s;
                }
            }
            if (last < end - spacing / 2.0f)
                regular.push_back({end, 0, 0, EventKind::Point, line.At(end)});
        }
        for (Event const& point : regular)
        {
            bool nearOther = false;
            for (Event const& event : events)
                if (event.kind == EventKind::Pull || event.kind == EventKind::Visit)
                    nearOther = nearOther || std::fabs(event.s - point.s) < spacing / 2.0f;
            if (!nearOther)
                events.push_back(point);
        }
        events.insert(events.end(), marks.begin(), marks.end());

        std::stable_sort(events.begin(), events.end(), [](Event const& a, Event const& b) {
            if (a.s != b.s)
                return a.s < b.s;
            if (a.order != b.order)
                return a.order < b.order;
            return a.sub < b.sub;
        });
        if (input.nav)
            events = ApplyHandOrder(events, hand.order, packs, spawns, settings, *input.nav, result.warnings);

        std::vector<std::size_t> foughtAt(packs.size(), events.size());
        for (std::size_t e = 0; e < events.size(); ++e)
            if (events[e].kind == EventKind::Pack)
                foughtAt[events[e].pack] = e;

        // The names: bosses by name, the other packs pack1, pack2, ...
        Route& route = result.route;
        route.name = input.trace.name;
        route.mapId = input.trace.mapId;
        route.bosses = input.bosses;
        std::vector<std::string> packName(packs.size());
        unsigned packNumber = 0;
        unsigned markNumber = 0;
        for (Event& event : events)
        {
            if (event.kind == EventKind::Mark)
            {
                do
                    event.name = "mark" + std::to_string(++markNumber);
                while (used.count(event.name));
                used.insert(event.name);
            }
            if (event.kind != EventKind::Pack)
                continue;
            std::size_t const p = event.pack;
            std::string const boss = bossOf[p] && input.entryName ? StepNameOf(input.entryName(bossOf[p])) : "";
            if (!boss.empty() && !used.count(boss))
                packName[p] = boss;
            else
            {
                do
                    packName[p] = "pack" + std::to_string(++packNumber);
                while (used.count(packName[p]));
            }
            used.insert(packName[p]);
        }

        for (std::size_t e = 0; e < events.size(); ++e)
        {
            Event const& event = events[e];
            if (event.kind == EventKind::Pack)
            {
                std::vector<RouteMob> mobs;
                for (std::size_t member : packs[event.pack].members)
                    mobs.push_back({spawns[member].guid, spawns[member].entry});
                AddPack(route, packName[event.pack], event.center, mobs);
                continue;
            }
            std::string name = event.name;
            if (event.kind == EventKind::Pull)
                name = "pull_" + packName[event.pack];
            AddPoint(route, name, {event.center, settings.pointRadius});
            route.points.back().quest = event.quest;

            // The trace decides where the points are. A point that a later
            // pack would notice is for the GM to look at.
            for (std::size_t p = 0; p < packs.size(); ++p)
                if (foughtAt[p] > e && DistanceToPack(event.center, packs[p], spawns) < settings.aggroRange)
                {
                    result.warnings.push_back("the point " + route.points.back().name + " at " + Format(event.center) +
                        " is within aggro range of a pack that the route fights later");
                    break;
                }
            if (event.kind == EventKind::Pull &&
                DistanceToPack(event.center, packs[event.pack], spawns) < settings.aggroRange)
                result.warnings.push_back("the pull point of " + packName[event.pack] +
                    " is within its aggro range: the trace first comes near it at its start or after a gap");
        }

        for (std::string const& warning : SkippedPackWarnings(route, packs, spawns, hand.skipped, settings.aggroRange))
            result.warnings.push_back(warning);
        result.ok = true;
        return result;
    }
}
