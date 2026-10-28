#include "RouteExport.h"

#include "DataFields.h"
#include "DataTokenizer.h"

#include <sstream>

namespace Bots
{
    namespace
    {
        using DataFields::ReadNumber;
        using DataFields::ReadFloat;
        using DataFields::ReadPosition;
        using DataFields::FormatFloat;
        using DataFields::FormatPosition;

        bool SamePosition(Position const& a, Position const& b)
        {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }

        bool SamePositions(std::vector<Position> const& a, std::vector<Position> const& b)
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (!SamePosition(a[i], b[i]))
                    return false;
            return true;
        }

        bool SameObjective(QuestObjective const& a, QuestObjective const& b)
        {
            return a.kind == b.kind && a.point == b.point && a.entry == b.entry && a.count == b.count &&
                a.item == b.item && a.entries == b.entries;
        }

        bool SameQuest(GeneratorQuest const& a, GeneratorQuest const& b)
        {
            if (a.id != b.id || a.objectives.size() != b.objectives.size() || a.visits.size() != b.visits.size())
                return false;
            for (std::size_t i = 0; i < a.objectives.size(); ++i)
                if (!SameObjective(a.objectives[i], b.objectives[i]))
                    return false;
            for (std::size_t i = 0; i < a.visits.size(); ++i)
                if (a.visits[i].name != b.visits[i].name || !SamePosition(a.visits[i].position, b.visits[i].position))
                    return false;
            return true;
        }

        // A name on one line, with no comment marker.
        std::string CleanName(std::string const& name)
        {
            std::string clean;
            for (char c : name)
                if (c != CommentMarker && c != '\n' && c != '\r')
                    clean += c;
            return clean;
        }

        void WriteNames(std::ostringstream& out, char const* key, std::map<std::uint32_t, std::string> const& names)
        {
            for (auto const& name : names)
            {
                std::string const clean = CleanName(name.second);
                if (clean.find_first_not_of(" \t") != std::string::npos)
                    out << key << " " << name.first << " " << clean << "\n";
            }
        }

        std::string JoinFrom(std::vector<std::string> const& words, std::size_t at)
        {
            std::string text;
            for (std::size_t i = at; i < words.size(); ++i)
                text += (i > at ? " " : "") + words[i];
            return text;
        }

        // Reads one line into the export. Returns an error, or "".
        std::string ReadLine(std::vector<std::string> const& words, MapExport& map, bool& started)
        {
            std::string const& key = words[0];
            if (!started)
            {
                if (key != "export" || words.size() != 2 || !ReadNumber(words[1], map.mapId))
                    return "the file starts with: export <map id>";
                started = true;
                return "";
            }
            if (key == "export")
                return "a second export line";
            if (key == "boss")
            {
                std::uint32_t entry = 0;
                if (words.size() != 2 || !ReadNumber(words[1], entry))
                    return "use: boss <entry>";
                map.bosses.push_back(entry);
                return "";
            }
            if (key == "pack")
            {
                std::uint32_t guid = 0;
                if (words.size() != 2 || !ReadNumber(words[1], guid))
                    return "use: pack <key>";
                if (!map.packs.empty() && map.packs.back().mobs.empty())
                    return "the pack " + std::to_string(map.packs.back().key) + " has no mob";
                map.packs.push_back({guid, {}});
                return "";
            }
            if (key == "mob")
            {
                ExportMob mob{0, 0, {}, 0.0f, {}};
                if (words.size() != 7 || !ReadNumber(words[1], mob.guid) || !ReadNumber(words[2], mob.entry) ||
                    !ReadPosition(words, 3, mob.position) || !ReadFloat(words[6], mob.wanderDistance))
                    return "use: mob <guid> <entry> <x> <y> <z> <wander>";
                if (map.packs.empty())
                    return "a mob line before any pack line";
                ExportPack& pack = map.packs.back();
                if (pack.mobs.empty() && mob.guid != pack.key)
                    return "the first mob of the pack " + std::to_string(pack.key) + " has another guid";
                pack.mobs.push_back(mob);
                return "";
            }
            if (key == "wp")
            {
                Position position{};
                if (words.size() != 4 || !ReadPosition(words, 1, position))
                    return "use: wp <x> <y> <z>";
                if (map.packs.empty() || map.packs.back().mobs.empty())
                    return "a wp line before any mob line";
                map.packs.back().mobs.back().patrol.push_back(position);
                return "";
            }
            if (key == "quest")
            {
                std::uint32_t id = 0;
                if (words.size() != 2 || !ReadNumber(words[1], id))
                    return "use: quest <id>";
                map.quests.push_back({id, {}, {}});
                return "";
            }
            if (key == "kill" || key == "item" || key == "visit")
            {
                if (map.quests.empty())
                    return "a " + key + " line before any quest line";
                GeneratorQuest& quest = map.quests.back();
                if (key == "visit")
                {
                    GeneratorVisit visit;
                    if (words.size() < 5 || !ReadPosition(words, 1, visit.position))
                        return "use: visit <x> <y> <z> <name>";
                    visit.name = JoinFrom(words, 4);
                    quest.visits.push_back(visit);
                    return "";
                }
                QuestObjective objective{key == "kill" ? QuestObjectiveKind::Kill : QuestObjectiveKind::Item};
                if (key == "kill")
                {
                    if (words.size() != 3 || !ReadNumber(words[1], objective.entry) || !ReadNumber(words[2], objective.count))
                        return "use: kill <entry> <count>";
                }
                else
                {
                    if (words.size() < 4 || !ReadNumber(words[1], objective.item) || !ReadNumber(words[2], objective.count))
                        return "use: item <item> <count> <entry> ...";
                    for (std::size_t i = 3; i < words.size(); ++i)
                    {
                        std::uint32_t entry = 0;
                        if (!ReadNumber(words[i], entry))
                            return "use: item <item> <count> <entry> ...";
                        objective.entries.push_back(entry);
                    }
                }
                quest.objectives.push_back(objective);
                return "";
            }
            std::map<std::string, std::map<std::uint32_t, std::string>*> const names = {
                {"creature", &map.creatureNames}, {"questname", &map.questNames}, {"itemname", &map.itemNames}};
            auto it = names.find(key);
            if (it != names.end())
            {
                std::uint32_t id = 0;
                if (words.size() < 3 || !ReadNumber(words[1], id))
                    return "use: " + key + " <id> <name>";
                (*it->second)[id] = JoinFrom(words, 2);
                return "";
            }
            return "unknown line " + key;
        }
    }

    bool operator==(ExportMob const& a, ExportMob const& b)
    {
        return a.guid == b.guid && a.entry == b.entry && SamePosition(a.position, b.position) &&
            a.wanderDistance == b.wanderDistance && SamePositions(a.patrol, b.patrol);
    }

    bool operator==(ExportPack const& a, ExportPack const& b)
    {
        return a.key == b.key && a.mobs == b.mobs;
    }

    bool operator==(MapExport const& a, MapExport const& b)
    {
        if (a.mapId != b.mapId || a.bosses != b.bosses || !(a.packs == b.packs) || a.quests.size() != b.quests.size() ||
            a.creatureNames != b.creatureNames || a.questNames != b.questNames || a.itemNames != b.itemNames)
            return false;
        for (std::size_t i = 0; i < a.quests.size(); ++i)
            if (!SameQuest(a.quests[i], b.quests[i]))
                return false;
        return true;
    }

    MapExport MakeExport(std::uint32_t mapId, std::vector<GeneratorSpawn> const& spawns,
        std::vector<std::uint32_t> const& bosses, std::vector<GeneratorQuest> const& quests, float clusterDistance)
    {
        MapExport map;
        map.mapId = mapId;
        map.bosses = bosses;
        map.quests = quests;
        for (GeneratorPack const& pack : BuildPacks(spawns, clusterDistance))
        {
            ExportPack exported{spawns[pack.members.front()].guid, {}};
            for (std::size_t member : pack.members)
            {
                GeneratorSpawn const& spawn = spawns[member];
                exported.mobs.push_back({spawn.guid, spawn.entry, spawn.position, spawn.wanderDistance, spawn.patrol});
            }
            map.packs.push_back(exported);
        }
        return map;
    }

    std::string ExportFileName(std::uint32_t mapId)
    {
        return "map_" + std::to_string(mapId) + ".export.txt";
    }

    std::string WriteExport(MapExport const& map)
    {
        std::ostringstream out;
        out << "# The export of map " << map.mapId << " (.route export, WOWAI#150): every pack, patrol,\n"
            << "# quest objective and boss. The dungeon map reads it. Run the export again for\n"
            << "# each content patch.\n";
        out << "export " << map.mapId << "\n";
        for (std::uint32_t boss : map.bosses)
            out << "boss " << boss << "\n";
        for (ExportPack const& pack : map.packs)
        {
            out << "pack " << pack.key << "\n";
            for (ExportMob const& mob : pack.mobs)
            {
                out << "mob " << mob.guid << " " << mob.entry << " " << FormatPosition(mob.position) << " "
                    << FormatFloat(mob.wanderDistance) << "\n";
                for (Position const& waypoint : mob.patrol)
                    out << "wp " << FormatPosition(waypoint) << "\n";
            }
        }
        for (GeneratorQuest const& quest : map.quests)
        {
            out << "quest " << quest.id << "\n";
            for (QuestObjective const& objective : quest.objectives)
            {
                if (objective.kind == QuestObjectiveKind::Kill)
                    out << "kill " << objective.entry << " " << objective.count << "\n";
                else if (objective.kind == QuestObjectiveKind::Item)
                {
                    out << "item " << objective.item << " " << objective.count;
                    for (std::uint32_t entry : objective.entries)
                        out << " " << entry;
                    out << "\n";
                }
            }
            for (GeneratorVisit const& visit : quest.visits)
                out << "visit " << FormatPosition(visit.position) << " " << CleanName(visit.name) << "\n";
        }
        WriteNames(out, "creature", map.creatureNames);
        WriteNames(out, "questname", map.questNames);
        WriteNames(out, "itemname", map.itemNames);
        return out.str();
    }

    ExportResult ParseExport(std::string const& text)
    {
        ExportResult result;
        bool started = false;
        for (DataLine const& line : TokenizeLines(text))
        {
            std::string const error = ReadLine(line.words, result.map, started);
            if (!error.empty())
            {
                result.map = MapExport();
                result.error = "line " + std::to_string(line.number) + ": " + error;
                return result;
            }
        }
        if (!started)
            result.error = "the file has no export line";
        else if (!result.map.packs.empty() && result.map.packs.back().mobs.empty())
            result.error = "the pack " + std::to_string(result.map.packs.back().key) + " has no mob";
        else
            result.ok = true;
        if (!result.ok)
            result.map = MapExport();
        return result;
    }
}
