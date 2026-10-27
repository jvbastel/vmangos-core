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
// The settings are seed, aggro, margin, pull, level, levelslack, cluster,
// spacing, radius, side, alt, gap, wide, wall, and bosses (entries split by commas, in run order). With
// no bosses setting, the boss list of the existing route file is used. See
// RouteGenerator.h of the bot library for what each one does.
// A ghost route name, for example ghost_389, makes the ghost route of that
// dungeon instead (bots/GhostWalk.h): stand at the graveyard of the dungeon.
//
// The route build command makes the route file around the GM trace of the
// route (RouteTraceCommands.cpp, bots/RouteTrace.h), with the same packs and
// quests, and with no navmesh path of its own:
//   .route build <route> [character] [setting=value ...]
// The settings are aggro, margin, cluster, spacing, radius, visit, and
// bosses. With a character on the map of the route, it also runs the walk
// check of the navmesh.

#include "BotGeometry.h"
#include "BotMgr.h"
#include "Chat.h"
#include "Database/DatabaseEnv.h"
#include "GameRouteNav.h"
#include "GhostWalk.h"
#include "Log.h"
#include "Map.h"
#include "Movement/MotionMaster.h"
#include "Movement/WaypointManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "GridMap.h"
#include "QuestDef.h"
#include "Route.h"
#include "RouteFiles.h"
#include "RouteGenerator.h"
#include "RouteTrace.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

namespace
{
    // A ghost route has a point at most this far after the last one, and it
    // loads the terrain this often along the straight line.
    float const GhostSpacingYards = 15.0f;
    float const GhostTileStepYards = 30.0f;

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
            {"aggro", &settings.aggroRange}, {"margin", &settings.margin}, {"pull", &settings.pullPathLimit},
            {"level", &settings.levelPathFactor}, {"levelslack", &settings.levelPathSlack},
            {"cluster", &settings.clusterDistance}, {"spacing", &settings.pointSpacing}, {"radius", &settings.pointRadius}, {"side", &settings.sideLimit},
            {"alt", &settings.alternativeLimit}, {"gap", &settings.alternativeGap}, {"wide", &settings.wideRoom},
            {"wall", &settings.wallClearance},
        };
        auto it = floats.find(key);
        if (it == floats.end())
            return "Unknown setting " + key + ". Use seed, aggro, margin, pull, level, levelslack, cluster, spacing, "
                "radius, side, alt, gap, wide, wall, or bosses.";
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

    // The boss list, the packs in route order, and the quests with their
    // objectives, as .route generate and .route build print them.
    void PrintSummary(ChatHandler& handler, Bots::Route const& route, std::vector<Bots::ObjectiveCheck> const& objectives)
    {
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
        for (Bots::ObjectiveCheck const& check : objectives)
        {
            Quest const* quest = sObjectMgr.GetQuestTemplate(check.quest);
            std::string text = "  " + std::to_string(check.quest) + " " + (quest ? quest->GetTitle() : "") + ": " +
                ObjectiveText(check.objective);
            if (check.objective.kind != Bots::QuestObjectiveKind::Visit)
                text += " (the route has " + std::to_string(check.have) + ")";
            handler.SendSysMessage(text.c_str());
        }
    }

    bool Fail(ChatHandler& handler, std::string const& line)
    {
        handler.SendSysMessage(line.c_str());
        return false;
    }
}

namespace
{
// A ghost route (bots/GhostWalk.h): from the character, who stands at the
// graveyard of the dungeon, along the navmesh to the spot in front of the
// dungeon portal, where the exit of the dungeon puts a player.
bool GenerateGhostRoute(ChatHandler& handler, std::string const& name, uint32 dungeon, Player* character)
{
    AreaTriggerTeleport const* exit = sObjectMgr.GetGoBackTrigger(dungeon);
    if (!exit || exit->destination.mapId != character->GetMapId())
        return Fail(handler, "Stand on the map of the portal of map " + std::to_string(dungeon) + ", at its graveyard.");
    GameRouteNav nav(character);
    if (!nav.Ready())
        return Fail(handler, "Map " + std::to_string(character->GetMapId()) + " has no navmesh. Are the mmaps loaded?");
    nav.SetStraightPaths(true);

    Bots::Position const from{character->GetPositionX(), character->GetPositionY(), character->GetPositionZ()};
    Bots::Position const to{exit->destination.x, exit->destination.y, exit->destination.z};
    // The navmesh tiles load with the terrain of their grid.
    TerrainInfo const* terrain = character->GetTerrain();
    float const length = Bots::Distance2d(from, to);
    for (float along = 0.0f; along <= length + GhostTileStepYards; along += GhostTileStepYards)
    {
        float const t = length > 0.0f ? std::min(1.0f, along / length) : 1.0f;
        terrain->GetHeightStatic(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, from.z + (to.z - from.z) * t);
    }
    std::vector<Bots::Position> path;
    if (!nav.Path(from, to, path))
    {
        std::ostringstream line;
        line << "No walk on the navmesh from here to the portal (" << to.x << " " << to.y << " " << to.z << ")";
        if (!path.empty())
            line << ": it ends after " << uint32(Bots::PathLength(path)) << " yards at " << path.back().x << " " << path.back().y << " "
                 << path.back().z;
        return Fail(handler, line.str() + ".");
    }

    Bots::Route const route = Bots::MakeGhostRoute(dungeon, character->GetMapId(), path, GhostSpacingYards);
    std::string const file = RouteFiles::PathOf(name);
    std::string const text = Bots::WriteRoute(route, Bots::RouteNames{});
    if (!RouteFiles::Write(file, text))
        return Fail(handler, "Cannot write " + file + ".");
    handler.PSendSysMessage("Ghost route %s: %u points, %.0f yards of path, in %s.", name.c_str(), uint32(route.points.size()),
        Bots::PathLength(path), file.c_str());
    return true;
}

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

    uint32 dungeon = 0;
    if (Bots::IsGhostRouteName(names[0], dungeon))
        return GenerateGhostRoute(handler, names[0], dungeon, character);

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
    PrintSummary(handler, route, generated.objectives);
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

namespace
{
// An unmet objective, for the GM.
std::string UnmetText(Bots::UnmetObjective const& unmet)
{
    Quest const* quest = sObjectMgr.GetQuestTemplate(unmet.quest);
    std::ostringstream text;
    text << "Unmet: quest " << unmet.quest << " " << (quest ? quest->GetTitle() : "") << ": " << ObjectiveText(unmet.objective);
    if (unmet.objective.kind == Bots::QuestObjectiveKind::Visit)
        text << ": the trace passes " << uint32(unmet.distance + 0.5f) << " yards from it.";
    else
        text << ": the trace has " << unmet.have << " of " << unmet.need << ".";
    return text.str();
}

bool RunBuild(ChatHandler& handler, char* args)
{
    std::vector<std::string> names;
    Bots::GeneratorSettings settings;
    std::vector<uint32> bosses;
    bool bossSetting = false;
    float visitRange = Bots::TraceBuildInput{}.visitRange;
    for (std::string const& word : Words(args))
    {
        std::size_t const equals = word.find('=');
        if (equals == std::string::npos)
        {
            names.push_back(word);
            continue;
        }
        std::string const key = word.substr(0, equals);
        if (key == "visit")
        {
            if (!ReadFloat(word.substr(equals + 1), visitRange))
                return Fail(handler, "The setting visit needs a number of 0 or more.");
            continue;
        }
        if (key != "aggro" && key != "margin" && key != "cluster" && key != "spacing" && key != "radius" &&
            key != "bosses")
            return Fail(handler, "Unknown setting " + key + ". Use aggro, margin, cluster, spacing, radius, visit, or bosses.");
        bossSetting = bossSetting || key == "bosses";
        std::string const error = ReadSetting(word, settings, bosses);
        if (!error.empty())
            return Fail(handler, error);
    }
    if (names.empty() || names.size() > 2)
        return Fail(handler, "Usage: .route build <route> [character] [setting=value ...]");
    if (!Bots::IsValidRouteName(names[0]))
        return Fail(handler, "The route name must have only a-z, 0-9, and _.");

    std::string const tracePath = RouteFiles::TracePathOf(names[0]);
    std::string text;
    if (!RouteFiles::Read(tracePath, text))
        return Fail(handler, "No trace " + tracePath + ". Walk it first with .route trace start " + names[0] + ".");
    Bots::TraceResult const trace = Bots::ParseTrace(text);
    if (!trace.ok)
        return Fail(handler, tracePath + ": " + trace.error);
    if (trace.trace.points.empty())
        return Fail(handler, "The trace " + tracePath + " has no points.");
    uint32 const mapId = trace.trace.mapId;

    // The character is only for the walk check.
    Player* character = nullptr;
    if (names.size() == 2)
    {
        std::string name = names[1];
        if (!normalizePlayerName(name) || !(character = ObjectAccessor::FindPlayerByName(name.c_str())))
            return Fail(handler, "No character " + names[1] + " is online.");
    }
    else if (handler.GetSession())
        character = handler.GetSession()->GetPlayer();
    if (character && (!character->IsInWorld() || character->GetMapId() != mapId))
        character = nullptr;

    std::string const path = RouteFiles::PathOf(names[0]);
    if (!bossSetting && RouteFiles::Read(path, text))
    {
        Bots::RouteResult const old = Bots::ParseRoute(text);
        if (old.ok && old.route.mapId == mapId)
            bosses = old.route.bosses;
    }
    if (bosses.empty())
        return Fail(handler, "Give the bosses in run order: bosses=<entry>,<entry>,...");

    Bots::Position const first = trace.trace.points.front().position;
    uint32 const zoneId = sTerrainMgr.GetZoneId(mapId, first.x, first.y, first.z);

    Bots::TraceBuildInput input;
    input.trace = trace.trace;
    input.spawns = SpawnsOf(mapId);
    input.bosses = bosses;
    input.settings = settings;
    input.visitRange = visitRange;
    input.entryName = RouteFiles::EntryName;
    input.quests = QuestsOf(zoneId, input.spawns);

    Bots::TraceBuild const built = Bots::BuildTraceRoute(input);
    if (!built.ok)
        return Fail(handler, "The route was not made: " + built.error);
    Bots::Route const& route = built.route;
    if (!RouteFiles::Write(path, Bots::WriteRoute(route, RouteFiles::Names())))
        return Fail(handler, "Cannot write " + path + ". Does the routes folder exist?");

    uint32 const checkpoints = uint32(Bots::MarkCount(trace.trace));
    handler.PSendSysMessage("Route %s on map %u from the trace (%u trace points, %.0f yards): %u steps, %u points "
        "(%u checkpoints), %u packs. File %s.", route.name.c_str(), mapId, uint32(trace.trace.points.size()),
        Bots::TraceLength(trace.trace), uint32(route.steps.size()), uint32(route.points.size()), checkpoints,
        uint32(route.packs.size()), path.c_str());
    PrintSummary(handler, route, built.objectives);

    for (uint32 boss : built.unmetBosses)
        handler.PSendSysMessage("Unmet: the trace does not pass the boss %u %s within aggro range (%.0f yards).", boss,
            RouteFiles::EntryName(boss).c_str(), settings.aggroRange);
    for (Bots::UnmetObjective const& unmet : built.unmet)
    {
        handler.SendSysMessage(UnmetText(unmet).c_str());
        for (Bots::NearPack const& pack : unmet.nearest)
        {
            std::ostringstream line;
            line << "  Nearest: " << pack.count << " in the pack of spawn " << pack.mobs.front().spawnGuid << " "
                 << RouteFiles::EntryName(pack.mobs.front().entry) << " at " << pack.position.x << " " << pack.position.y
                 << " " << pack.position.z << ", " << uint32(pack.distance + 0.5f) << " yards from the trace.";
            handler.SendSysMessage(line.str().c_str());
        }
        if (unmet.nearest.empty() && unmet.objective.kind != Bots::QuestObjectiveKind::Visit)
            handler.SendSysMessage("  The dungeon has no more spawns for it.");
    }
    if (built.unmet.empty() && built.unmetBosses.empty())
        handler.SendSysMessage("Each boss and each quest objective is on the trace.");
    else
        handler.PSendSysMessage("Walk an extra trace there (.route trace start %s), then run .route build again.",
            route.name.c_str());
    for (std::string const& warning : built.warnings)
        handler.PSendSysMessage("Check: %s", warning.c_str());

    uint32 problems = 0;
    if (character)
    {
        GameRouteNav nav(character);
        if (nav.Ready())
        {
            std::vector<std::string> const lines = Bots::CheckRouteWalk(route, nav);
            problems = uint32(lines.size());
            if (lines.empty())
                handler.SendSysMessage("Walk check: each point is on the navmesh, and each path to the next point has no lava.");
            for (std::string const& line : lines)
                handler.PSendSysMessage("Walk check: %s", line.c_str());
        }
    }
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] .route build wrote %s: %u steps, %u packs, %u unmet, %u walk problems.",
        path.c_str(), uint32(route.steps.size()), uint32(route.packs.size()),
        uint32(built.unmet.size() + built.unmetBosses.size()), problems);
    return true;
}
}

bool ChatHandler::HandleRouteBuildCommand(char* args)
{
    if (RunBuild(*this, args))
        return true;
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteGenerateCommand(char* args)
{
    if (RunGenerate(*this, args))
        return true;
    SetSentErrorMessage(true);
    return false;
}
