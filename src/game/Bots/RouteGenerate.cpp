/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// The route generator command. It builds the route file of the character's
// map from the data that the server loaded: the spawns, the creature groups,
// the patrols, the quests and their drops, and the navmesh.
//   .route generate <route> [character] [setting=value ...]
// The settings are seed, aggro, margin, cluster, spacing, radius, side, alt,
// gap, wide, wall, and bosses (entries split by commas, in run order). With
// no bosses setting, the boss list of the existing route file is used. See
// RouteGenerator.h of the bot library for what each one does.

#include "BotMgr.h"
#include "Chat.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Map.h"
#include "MoveMap.h"
#include "MoveMapSharedDefines.h"
#include "Movement/MotionMaster.h"
#include "Movement/WaypointManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PathFinder.h"
#include "Player.h"
#include "QuestDef.h"
#include "Route.h"
#include "RouteFiles.h"
#include "RouteGenerator.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

namespace
{
    float Distance(Bots::Position const& a, Bots::Position const& b)
    {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    }

    // The navmesh of the map, for the route generator. Paths come from
    // PathInfo with the character as the walker, so they leave out lava like
    // the path of a walking player.
    class GameRouteNav : public Bots::RouteNav
    {
    public:
        explicit GameRouteNav(Player* walker) : m_walker(walker)
        {
            m_query = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMeshQuery(walker->GetMapId());
            m_ground.setIncludeFlags(NAV_GROUND);
            m_ground.setExcludeFlags(NAV_MAGMA | NAV_SLIME);
            m_lava.setIncludeFlags(NAV_MAGMA | NAV_SLIME);
        }

        bool Ready() const { return m_query != nullptr; }

        bool Path(Bots::Position const& from, Bots::Position const& to, std::vector<Bots::Position>& out) override
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

        Bots::Position Reach(Bots::Position const& from, float dx, float dy, float distance) override
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

        bool OnMesh(Bots::Position const& position) override
        {
            return Inside(position, m_ground);
        }

        bool OnLava(Bots::Position const& position)
        {
            return Inside(position, m_lava) && !Inside(position, m_ground);
        }

    private:
        // True when the position lies on a polygon that the filter takes.
        bool Inside(Bots::Position const& position, dtQueryFilter const& filter)
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

        Player* m_walker;
        dtNavMeshQuery const* m_query = nullptr;
        dtQueryFilter m_ground;
        dtQueryFilter m_lava;
    };

    std::vector<std::string> Words(char const* args)
    {
        std::vector<std::string> words;
        std::istringstream stream(args ? args : "");
        std::string word;
        while (stream >> word)
            words.push_back(word);
        return words;
    }

    bool ReadFloat(std::string const& text, float& value)
    {
        char* end = nullptr;
        float const number = std::strtof(text.c_str(), &end);
        if (text.empty() || *end != '\0' || !std::isfinite(number) || number < 0.0f)
            return false;
        value = number;
        return true;
    }

    // Reads one setting=value word. Returns an error line, or "".
    std::string ReadSetting(std::string const& word, Bots::GeneratorSettings& settings,
        std::vector<uint32>& bosses)
    {
        std::size_t const equals = word.find('=');
        std::string const key = word.substr(0, equals);
        std::string const value = word.substr(equals + 1);
        if (key == "seed")
        {
            char* end = nullptr;
            unsigned long const seed = std::strtoul(value.c_str(), &end, 10);
            if (value.empty() || *end != '\0')
                return "The seed must be a number.";
            settings.seed = uint32(seed);
            return "";
        }
        if (key == "bosses")
        {
            bosses.clear();
            std::istringstream list(value);
            std::string entry;
            while (std::getline(list, entry, ','))
            {
                char* end = nullptr;
                unsigned long const number = std::strtoul(entry.c_str(), &end, 10);
                if (entry.empty() || *end != '\0' || !number)
                    return "Use bosses=<entry>,<entry>,...";
                bosses.push_back(uint32(number));
            }
            return "";
        }
        std::map<std::string, float*> const floats = {
            {"aggro", &settings.aggroRange}, {"margin", &settings.margin}, {"cluster", &settings.clusterDistance},
            {"spacing", &settings.pointSpacing}, {"radius", &settings.pointRadius}, {"side", &settings.sideLimit},
            {"alt", &settings.alternativeLimit}, {"gap", &settings.alternativeGap}, {"wide", &settings.wideRoom},
            {"wall", &settings.wallClearance},
        };
        auto it = floats.find(key);
        if (it == floats.end())
            return "Unknown setting " + key + ". Use seed, aggro, margin, cluster, spacing, radius, side, alt, gap, "
                "wide, wall, or bosses.";
        if (!ReadFloat(value, *it->second))
            return "The setting " + key + " needs a number of 0 or more.";
        if (key == "spacing" && *it->second < 1.0f)
            return "The spacing must be 1 yard or more.";
        return "";
    }

    bool IsHostile(CreatureInfo const* info)
    {
        FactionTemplateEntry const* faction = info ? sObjectMgr.GetFactionTemplateEntry(info->faction) : nullptr;
        return faction && (faction->IsHostileToPlayers() || (faction->hostileMask & FACTION_MASK_HORDE));
    }

    // The spawns of the map that the server loaded for its content patch.
    std::vector<Bots::GeneratorSpawn> SpawnsOf(uint32 mapId)
    {
        std::vector<Bots::GeneratorSpawn> spawns;
        auto collect = [&spawns, mapId](CreatureDataPair const& pair) {
            CreatureData const& data = pair.second;
            if (data.position.mapId != mapId || !data.creature_id[0])
                return false;
            Bots::GeneratorSpawn spawn{pair.first, data.creature_id[0],
                {data.position.x, data.position.y, data.position.z}};
            if (data.movement_type == RANDOM_MOTION_TYPE)
                spawn.wanderDistance = data.wander_distance;
            if (data.movement_type == WAYPOINT_MOTION_TYPE)
                if (WaypointPath const* path = sWaypointMgr.GetDefaultPath(spawn.entry, pair.first))
                    for (auto const& node : *path)
                        spawn.patrol.push_back({node.second.x, node.second.y, node.second.z});
            spawn.hostile = IsHostile(sObjectMgr.GetCreatureTemplate(spawn.entry));
            spawns.push_back(spawn);
            return false;
        };
        sObjectMgr.DoCreatureData(collect);
        std::sort(spawns.begin(), spawns.end(),
            [](Bots::GeneratorSpawn const& a, Bots::GeneratorSpawn const& b) { return a.guid < b.guid; });

        // The creature groups of these spawns.
        std::map<uint32, std::size_t> byGuid;
        for (std::size_t i = 0; i < spawns.size(); ++i)
            byGuid[spawns[i].guid] = i;
        if (auto result = WorldDatabase.Query("SELECT `leader_guid`, `member_guid` FROM `creature_groups`"))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 const leader = fields[0].GetUInt32();
                auto member = byGuid.find(fields[1].GetUInt32());
                if (member != byGuid.end() && byGuid.count(leader) && leader != member->first)
                    spawns[member->second].groupLeader = leader;
            } while (result->NextRow());
        }
        return spawns;
    }

    // The creature entries that drop the item, from the creature loot.
    std::vector<uint32> DroppersOf(uint32 item, std::set<uint32> const& entriesOnMap)
    {
        std::vector<uint32> entries;
        if (auto result = WorldDatabase.PQuery("SELECT DISTINCT t.`entry` FROM `creature_template` t "
                "JOIN `creature_loot_template` l ON l.`entry` = t.`loot_id` WHERE l.`item` = %u ORDER BY t.`entry`", item))
        {
            do
            {
                uint32 const entry = result->Fetch()[0].GetUInt32();
                if (entriesOnMap.count(entry))
                    entries.push_back(entry);
            } while (result->NextRow());
        }
        return entries;
    }

    // The quests of the zone, and the quests that a spawn of the map starts
    // or ends.
    std::vector<Bots::GeneratorQuest> QuestsOf(uint32 zoneId, std::vector<Bots::GeneratorSpawn> const& spawns)
    {
        std::set<uint32> hostileEntries;
        std::map<uint32, std::vector<std::size_t>> spawnsOfEntry;
        for (std::size_t i = 0; i < spawns.size(); ++i)
        {
            spawnsOfEntry[spawns[i].entry].push_back(i);
            if (spawns[i].hostile)
                hostileEntries.insert(spawns[i].entry);
        }

        std::map<uint32, std::vector<Bots::GeneratorVisit>> visits;
        for (auto const& entry : spawnsOfEntry)
        {
            std::set<uint32> quests;
            QuestRelationsMapBounds starts = sObjectMgr.GetCreatureQuestRelationsMapBounds(entry.first);
            for (auto it = starts.first; it != starts.second; ++it)
                quests.insert(it->second);
            QuestRelationsMapBounds ends = sObjectMgr.GetCreatureQuestInvolvedRelationsMapBounds(entry.first);
            for (auto it = ends.first; it != ends.second; ++it)
                quests.insert(it->second);
            for (uint32 quest : quests)
                for (std::size_t i : entry.second)
                    visits[quest].push_back({RouteFiles::EntryName(entry.first), spawns[i].position});
        }

        std::vector<uint32> ids;
        for (auto const& quest : sObjectMgr.GetQuestTemplates())
            if (quest.second->GetZoneOrSort() == int32(zoneId) || visits.count(quest.first))
                ids.push_back(quest.first);
        std::sort(ids.begin(), ids.end());

        std::vector<Bots::GeneratorQuest> result;
        for (uint32 id : ids)
        {
            Quest const* quest = sObjectMgr.GetQuestTemplate(id);
            Bots::GeneratorQuest generated{id, {}, visits[id]};
            for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                if (quest->ReqCreatureOrGOId[i] > 0 && quest->ReqCreatureOrGOCount[i])
                {
                    Bots::QuestObjective kill{Bots::QuestObjectiveKind::Kill};
                    kill.entry = uint32(quest->ReqCreatureOrGOId[i]);
                    kill.count = quest->ReqCreatureOrGOCount[i];
                    generated.objectives.push_back(kill);
                }
            for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
                if (quest->ReqItemId[i] && quest->ReqItemCount[i])
                {
                    Bots::QuestObjective item{Bots::QuestObjectiveKind::Item};
                    item.item = quest->ReqItemId[i];
                    item.count = quest->ReqItemCount[i];
                    item.entries = DroppersOf(item.item, hostileEntries);
                    if (!item.entries.empty())
                        generated.objectives.push_back(item);
                }
            if (!generated.objectives.empty() || !generated.visits.empty())
                result.push_back(generated);
        }
        return result;
    }

    std::string ObjectiveText(Bots::QuestObjective const& objective)
    {
        std::ostringstream text;
        switch (objective.kind)
        {
            case Bots::QuestObjectiveKind::Visit:
                text << "visit " << objective.point;
                break;
            case Bots::QuestObjectiveKind::Kill:
                text << "kill " << objective.count << " " << RouteFiles::EntryName(objective.entry);
                break;
            case Bots::QuestObjectiveKind::Item:
            {
                ItemPrototype const* item = sObjectMgr.GetItemPrototype(objective.item);
                text << objective.count << " " << (item ? item->Name1 : "item") << " from";
                for (uint32 entry : objective.entries)
                    text << " " << RouteFiles::EntryName(entry);
                break;
            }
        }
        return text.str();
    }

    bool Fail(ChatHandler& handler, std::string const& line)
    {
        handler.SendSysMessage(line.c_str());
        return false;
    }
}

namespace
{
bool RunGenerate(ChatHandler& handler, char* args)
{
    std::vector<std::string> names;
    Bots::GeneratorSettings settings;
    std::vector<uint32> bosses;
    bool bossSetting = false;
    for (std::string const& word : Words(args))
    {
        if (word.find('=') == std::string::npos)
        {
            names.push_back(word);
            continue;
        }
        bossSetting = bossSetting || word.compare(0, 7, "bosses=") == 0;
        std::string const error = ReadSetting(word, settings, bosses);
        if (!error.empty())
            return Fail(handler, error);
    }
    if (names.empty() || names.size() > 2)
        return Fail(handler, "Usage: .route generate <route> [character] [setting=value ...]");
    if (!Bots::IsValidRouteName(names[0]))
        return Fail(handler, "The route name must have only a-z, 0-9, and _.");

    Player* character = nullptr;
    if (names.size() == 2)
    {
        std::string name = names[1];
        if (!normalizePlayerName(name) || !(character = ObjectAccessor::FindPlayerByName(name.c_str())))
            return Fail(handler, "No character " + names[1] + " is online.");
    }
    else if (handler.GetSession())
        character = handler.GetSession()->GetPlayer();
    if (!character || !character->IsInWorld())
        return Fail(handler, "On the console, name an online character: .route generate <route> <character>");
    if (character->CanFly())
        return Fail(handler, "Turn off flying first: the navmesh paths are for a walking character.");

    uint32 const mapId = character->GetMapId();
    std::string const path = RouteFiles::PathOf(names[0]);
    if (!bossSetting)
    {
        std::string text;
        if (RouteFiles::Read(path, text))
        {
            Bots::RouteResult const old = Bots::ParseRoute(text);
            if (old.ok && old.route.mapId == mapId)
                bosses = old.route.bosses;
        }
    }
    if (bosses.empty())
        return Fail(handler, "Give the bosses in run order: bosses=<entry>,<entry>,...");

    GameRouteNav nav(character);
    if (!nav.Ready())
        return Fail(handler, "Map " + std::to_string(mapId) + " has no navmesh. Are the mmaps loaded?");

    AreaTriggerTeleport const* entrance = sObjectMgr.GetMapEntranceTrigger(mapId);
    if (!entrance)
        return Fail(handler, "Map " + std::to_string(mapId) + " has no entrance area trigger.");

    Bots::GeneratorInput input;
    input.name = names[0];
    input.mapId = mapId;
    input.entrance = {entrance->destination.x, entrance->destination.y, entrance->destination.z};
    input.spawns = SpawnsOf(mapId);
    input.bosses = bosses;
    input.settings = settings;
    input.entryName = RouteFiles::EntryName;
    input.quests = QuestsOf(character->GetZoneId(), input.spawns);

    // The navmesh tiles load with the terrain of their grid.
    TerrainInfo const* terrain = character->GetTerrain();
    terrain->GetHeightStatic(input.entrance.x, input.entrance.y, input.entrance.z);
    for (Bots::GeneratorSpawn const& spawn : input.spawns)
        terrain->GetHeightStatic(spawn.position.x, spawn.position.y, spawn.position.z);

    Bots::GeneratedRoute const generated = Bots::GenerateRoute(input, nav);
    if (!generated.ok)
        return Fail(handler, "The route was not made: " + generated.error);
    Bots::Route const& route = generated.route;
    if (!RouteFiles::Write(path, Bots::WriteRoute(route, RouteFiles::Names())))
        return Fail(handler, "Cannot write " + path + ". Does the routes folder exist?");

    // The summary.
    uint32 alternatives = 0;
    for (Bots::RoutePoint const& point : route.points)
        alternatives += point.places.size() > 1 ? 1 : 0;
    handler.PSendSysMessage("Route %s on map %u, seed %u: %u steps, %u points (%u with alternatives), %u packs. File %s.",
        route.name.c_str(), mapId, route.seed, uint32(route.steps.size()), uint32(route.points.size()), alternatives,
        uint32(route.packs.size()), path.c_str());
    std::ostringstream bossLine;
    bossLine << "Bosses:";
    for (uint32 boss : route.bosses)
        bossLine << " " << boss << " " << RouteFiles::EntryName(boss) << ",";
    std::string bossText = bossLine.str();
    bossText.pop_back();
    handler.SendSysMessage(bossText.c_str());
    handler.SendSysMessage("Packs in order:");
    for (Bots::RouteStep const& step : route.steps)
    {
        if (step.kind != Bots::RouteStepKind::Pack)
            continue;
        Bots::RoutePack const& pack = route.packs[step.index];
        std::map<std::string, uint32> counts;
        for (Bots::RouteMob const& mob : pack.mobs)
            ++counts[RouteFiles::EntryName(mob.entry)];
        std::ostringstream line;
        line << "  " << pack.name << ":";
        for (auto const& count : counts)
            line << " " << count.second << " " << count.first << ",";
        std::string text = line.str();
        text.pop_back();
        if (pack.quest)
            text += " (quest " + std::to_string(pack.quest) + ")";
        handler.SendSysMessage(text.c_str());
    }
    handler.SendSysMessage("Quests:");
    for (Bots::ObjectiveCheck const& check : generated.objectives)
    {
        Quest const* quest = sObjectMgr.GetQuestTemplate(check.quest);
        std::string text = "  " + std::to_string(check.quest) + " " + (quest ? quest->GetTitle() : "") + ": " +
            ObjectiveText(check.objective);
        if (check.objective.kind != Bots::QuestObjectiveKind::Visit)
            text += " (the route has " + std::to_string(check.have) + ")";
        handler.SendSysMessage(text.c_str());
    }
    for (std::string const& warning : generated.warnings)
        handler.PSendSysMessage("Check: %s", warning.c_str());
    std::vector<std::string> const problems = Bots::CheckRouteWalk(route, nav);
    if (problems.empty())
        handler.SendSysMessage("Walk check: each place is on the navmesh, and each path to the next point has no lava.");
    for (std::string const& problem : problems)
        handler.PSendSysMessage("Walk check: %s", problem.c_str());
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] .route generate wrote %s: %u steps, %u packs, seed %u, %u walk problems.",
        path.c_str(), uint32(route.steps.size()), uint32(route.packs.size()), route.seed, uint32(problems.size()));
    return true;
}
}

bool ChatHandler::HandleRouteGenerateCommand(char* args)
{
    if (RunGenerate(*this, args))
        return true;
    SetSentErrorMessage(true);
    return false;
}
