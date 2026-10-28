#include "Route.h"

#include "BotGeometry.h"
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

        // A step name: letters, digits, and `_`, not only digits.
        bool IsValidStepName(std::string const& name)
        {
            bool letter = false;
            for (char c : name)
            {
                bool const digit = c >= '0' && c <= '9';
                bool const other = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
                if (!digit && !other)
                    return false;
                letter = letter || other;
            }
            return letter;
        }

        bool NameIsUsed(Route const& route, std::string const& name)
        {
            return FindPoint(route, name) || FindPack(route, name);
        }

        std::string Usage(std::string const& keyword)
        {
            if (keyword == "route")
                return "use: route <name> <map>";
            if (keyword == "boss")
                return "use: boss <entry>";
            if (keyword == "point")
                return "use: point <name> <x> <y> <z> <radius> [quest <id>]";
            if (keyword == "alt")
                return "use: alt <x> <y> <z> <radius>";
            if (keyword == "pack")
                return "use: pack <name> <x> <y> <z> [quest <id>]";
            if (keyword == "seed")
                return "use: seed <number>";
            if (keyword == "quest")
                return "use: quest <id>";
            if (keyword == "visit")
                return "use: visit <point>";
            if (keyword == "kill")
                return "use: kill <entry> <count>";
            if (keyword == "item")
                return "use: item <item> <count> <entry> [<entry>...]";
            return "use: mob <spawn guid> <entry>";
        }

        std::string CheckRadius(float radius)
        {
            return radius > 0.0f ? "" : "the radius must be more than 0";
        }

        std::string CheckNewName(Route const& route, std::string const& name)
        {
            if (!IsValidStepName(name))
                return "the name '" + name + "' must have only letters, digits, and _, and not only digits";
            if (NameIsUsed(route, name))
                return "the name '" + name + "' is already in the route";
            return "";
        }

        std::string FirstFreeName(Route const& route, char const* prefix)
        {
            for (unsigned number = 1;; ++number)
            {
                std::string const name = prefix + std::to_string(number);
                if (!NameIsUsed(route, name))
                    return name;
            }
        }

        std::string Comment(std::function<std::string(std::uint32_t)> const& entryName, std::uint32_t entry)
        {
            std::string const name = entryName ? entryName(entry) : "";
            return name.empty() ? "" : std::string(" ") + CommentMarker + " " + name;
        }

        std::string QuestTag(std::uint32_t quest)
        {
            return quest ? " quest " + std::to_string(quest) : "";
        }

        // Reads the optional `quest <id>` at `first` of a point or pack line.
        // False when the words do not fit.
        bool ReadQuestTag(std::vector<std::string> const& words, std::size_t first, std::uint32_t& quest)
        {
            quest = 0;
            if (words.size() == first)
                return true;
            return words.size() == first + 2 && words[first] == "quest" && ReadNumber(words[first + 1], quest) &&
                quest != 0;
        }

        std::string CheckQuestTag(Route const& route, std::uint32_t quest)
        {
            if (quest && !FindQuest(route, quest))
                return "the quest " + std::to_string(quest) + " is not in the quest list";
            return "";
        }
    }

    bool IsValidRouteName(std::string const& name)
    {
        if (name.empty() || name.size() > 64)
            return false;
        for (char c : name)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
                return false;
        return true;
    }

    RoutePoint const* FindPoint(Route const& route, std::string const& name)
    {
        for (RoutePoint const& point : route.points)
            if (point.name == name)
                return &point;
        return nullptr;
    }

    RoutePack const* FindPack(Route const& route, std::string const& name)
    {
        for (RoutePack const& pack : route.packs)
            if (pack.name == name)
                return &pack;
        return nullptr;
    }

    RouteQuest const* FindQuest(Route const& route, std::uint32_t id)
    {
        for (RouteQuest const& quest : route.quests)
            if (quest.id == id)
                return &quest;
        return nullptr;
    }

    std::vector<std::string> WalkBackPoints(Route const& route, Position const& from, std::string const& pack)
    {
        std::vector<RoutePoint const*> before;
        bool found = false;
        for (RouteStep const& step : route.steps)
        {
            if (step.kind == RouteStepKind::Pack && route.packs[step.index].name == pack)
            {
                found = true;
                break;
            }
            if (step.kind == RouteStepKind::Point && route.points[step.index].quest == 0)
                before.push_back(&route.points[step.index]);
        }
        std::vector<std::string> names;
        if (!found || before.empty())
            return names;
        std::size_t nearest = 0;
        for (std::size_t i = 1; i < before.size(); ++i)
            if (Distance3d(from, before[i]->places[0].position) < Distance3d(from, before[nearest]->places[0].position))
                nearest = i;
        for (std::size_t i = nearest; i < before.size(); ++i)
            names.push_back(before[i]->name);
        return names;
    }

    std::string AddPoint(Route& route, std::string name, RoutePlace const& place)
    {
        if (name.empty())
            name = FirstFreeName(route, "p");
        std::string error = CheckNewName(route, name);
        if (error.empty())
            error = CheckRadius(place.radius);
        if (!error.empty())
            return error;
        route.steps.push_back({RouteStepKind::Point, route.points.size()});
        route.points.push_back({name, {place}});
        return "";
    }

    std::string AddAlternative(Route& route, RoutePlace const& place)
    {
        if (route.points.empty())
            return "an alternative needs a point before it";
        std::string const error = CheckRadius(place.radius);
        if (!error.empty())
            return error;
        route.points.back().places.push_back(place);
        return "";
    }

    std::string AddPack(Route& route, std::string name, Position const& position,
        std::vector<RouteMob> const& mobs)
    {
        if (name.empty())
            name = FirstFreeName(route, "pack");
        std::string const error = CheckNewName(route, name);
        if (!error.empty())
            return error;
        route.steps.push_back({RouteStepKind::Pack, route.packs.size()});
        route.packs.push_back({name, position, {}});
        for (RouteMob const& mob : mobs)
        {
            std::string const mobError = AddMob(route, mob);
            if (!mobError.empty())
            {
                RemoveLastStep(route);
                return mobError;
            }
        }
        return "";
    }

    std::string AddMob(Route& route, RouteMob const& mob)
    {
        if (route.packs.empty())
            return "a mob needs a pack before it";
        for (RoutePack const& pack : route.packs)
            for (RouteMob const& other : pack.mobs)
                if (other.spawnGuid == mob.spawnGuid)
                    return "the mob " + std::to_string(mob.spawnGuid) + " is already in the pack " + pack.name;
        route.packs.back().mobs.push_back(mob);
        return "";
    }

    std::string AddMobTo(Route& route, std::string const& packName, RouteMob const& mob)
    {
        RoutePack* target = nullptr;
        for (RoutePack& pack : route.packs)
            if (pack.name == packName)
                target = &pack;
        if (!target)
            return "the route has no pack " + packName;
        for (RoutePack const& pack : route.packs)
            for (RouteMob const& other : pack.mobs)
                if (other.spawnGuid == mob.spawnGuid)
                    return "the mob " + std::to_string(mob.spawnGuid) + " is already in the pack " + pack.name;
        target->mobs.push_back(mob);
        return "";
    }

    std::string RemoveMob(Route& route, std::uint32_t spawnGuid)
    {
        for (RoutePack& pack : route.packs)
            for (auto it = pack.mobs.begin(); it != pack.mobs.end(); ++it)
                if (it->spawnGuid == spawnGuid)
                {
                    pack.mobs.erase(it);
                    return "";
                }
        return "the mob " + std::to_string(spawnGuid) + " is in no pack";
    }

    std::string AddBoss(Route& route, std::uint32_t entry)
    {
        for (std::uint32_t boss : route.bosses)
            if (boss == entry)
                return "the boss " + std::to_string(entry) + " is already in the boss list";
        route.bosses.push_back(entry);
        return "";
    }

    std::string RemoveLastStep(Route& route)
    {
        if (route.steps.empty())
            return "the route has no step";
        RouteStep const last = route.steps.back();
        route.steps.pop_back();
        // The last step is also the last of its kind.
        if (last.kind == RouteStepKind::Point)
            route.points.pop_back();
        else
            route.packs.pop_back();
        return "";
    }

    RouteResult ParseRoute(std::string const& text)
    {
        RouteResult result;
        result.ok = false;
        Route& route = result.route;
        bool haveHeader = false;
        bool haveSeed = false;
        // The visit objectives, checked at the end: a point can come later.
        std::vector<std::pair<std::size_t, std::string>> visits;

        auto fail = [&result](std::size_t lineNumber, std::string const& error) {
            result.route = Route();
            result.error = "line " + std::to_string(lineNumber) + ": " + error;
            return result;
        };

        for (DataLine const& line : TokenizeLines(text))
        {
            std::vector<std::string> const& words = line.words;
            std::string const& keyword = words[0];
            std::string error;
            auto wordCountIs = [&](std::size_t count) {
                if (words.size() != count)
                    error = Usage(keyword);
                return error.empty();
            };
            auto currentQuest = [&]() -> RouteQuest* {
                if (route.quests.empty())
                {
                    error = "an objective needs a quest before it";
                    return nullptr;
                }
                return &route.quests.back();
            };

            if (keyword == "route")
            {
                if (haveHeader)
                    error = "the file has more than one route line";
                else if (wordCountIs(3))
                {
                    if (!IsValidRouteName(words[1]))
                        error = "the route name '" + words[1] + "' must have only a-z, 0-9, and _";
                    else if (!ReadNumber(words[2], route.mapId))
                        error = Usage(keyword);
                    route.name = words[1];
                    haveHeader = true;
                }
            }
            else if (!haveHeader)
                error = "the file must start with: route <name> <map>";
            else if (keyword == "seed")
            {
                if (haveSeed)
                    error = "the file has more than one seed line";
                else if (wordCountIs(2))
                {
                    if (ReadNumber(words[1], route.seed))
                        route.hasSeed = haveSeed = true;
                    else
                        error = Usage(keyword);
                }
            }
            else if (keyword == "boss")
            {
                std::uint32_t entry = 0;
                if (wordCountIs(2))
                    error = ReadNumber(words[1], entry) ? AddBoss(route, entry) : Usage(keyword);
            }
            else if (keyword == "quest")
            {
                std::uint32_t id = 0;
                if (wordCountIs(2))
                {
                    if (!ReadNumber(words[1], id) || id == 0)
                        error = Usage(keyword);
                    else if (FindQuest(route, id))
                        error = "the quest " + words[1] + " is already in the quest list";
                    else
                        route.quests.push_back({id, {}});
                }
            }
            else if (keyword == "visit")
            {
                RouteQuest* quest = currentQuest();
                if (quest && wordCountIs(2))
                {
                    QuestObjective objective{QuestObjectiveKind::Visit};
                    objective.point = words[1];
                    quest->objectives.push_back(objective);
                    visits.emplace_back(line.number, words[1]);
                }
            }
            else if (keyword == "kill")
            {
                RouteQuest* quest = currentQuest();
                QuestObjective objective{QuestObjectiveKind::Kill};
                if (quest && wordCountIs(3))
                {
                    if (!ReadNumber(words[1], objective.entry) || !ReadNumber(words[2], objective.count))
                        error = Usage(keyword);
                    else if (objective.count == 0)
                        error = "the count must be more than 0";
                    else
                        quest->objectives.push_back(objective);
                }
            }
            else if (keyword == "item")
            {
                RouteQuest* quest = currentQuest();
                QuestObjective objective{QuestObjectiveKind::Item};
                if (quest)
                {
                    bool ok = words.size() >= 4 && ReadNumber(words[1], objective.item) &&
                        ReadNumber(words[2], objective.count);
                    for (std::size_t i = 3; ok && i < words.size(); ++i)
                    {
                        std::uint32_t entry = 0;
                        ok = ReadNumber(words[i], entry);
                        objective.entries.push_back(entry);
                    }
                    if (!ok)
                        error = Usage(keyword);
                    else if (objective.count == 0)
                        error = "the count must be more than 0";
                    else
                        quest->objectives.push_back(objective);
                }
            }
            else if (keyword == "point")
            {
                RoutePlace place{};
                std::uint32_t quest = 0;
                if (words.size() >= 6 && ReadPosition(words, 2, place.position) && ReadFloat(words[5], place.radius) &&
                    ReadQuestTag(words, 6, quest))
                {
                    error = CheckQuestTag(route, quest);
                    if (error.empty())
                        error = AddPoint(route, words[1], place);
                    if (error.empty())
                        route.points.back().quest = quest;
                }
                else
                    error = Usage(keyword);
            }
            else if (keyword == "alt")
            {
                RoutePlace place{};
                if (wordCountIs(5))
                    error = ReadPosition(words, 1, place.position) && ReadFloat(words[4], place.radius)
                        ? AddAlternative(route, place) : Usage(keyword);
            }
            else if (keyword == "pack")
            {
                Position position{};
                std::uint32_t quest = 0;
                if (words.size() >= 5 && ReadPosition(words, 2, position) && ReadQuestTag(words, 5, quest))
                {
                    error = CheckQuestTag(route, quest);
                    if (error.empty())
                        error = AddPack(route, words[1], position, {});
                    if (error.empty())
                        route.packs.back().quest = quest;
                }
                else
                    error = Usage(keyword);
            }
            else if (keyword == "mob")
            {
                RouteMob mob{};
                if (wordCountIs(3))
                    error = ReadNumber(words[1], mob.spawnGuid) && ReadNumber(words[2], mob.entry)
                        ? AddMob(route, mob) : Usage(keyword);
            }
            else
                error = "unknown word '" + keyword +
                    "'. Use route, seed, boss, quest, visit, kill, item, point, alt, pack, or mob";

            if (!error.empty())
                return fail(line.number, error);
        }

        if (!haveHeader)
        {
            result.error = "the file has no route line";
            return result;
        }
        for (auto const& visit : visits)
            if (!FindPoint(route, visit.second))
                return fail(visit.first, "the route has no point " + visit.second);
        result.ok = true;
        return result;
    }

    std::string WriteRoute(Route const& route,
        std::function<std::string(std::uint32_t entry)> const& entryName)
    {
        RouteNames names;
        names.entry = entryName;
        return WriteRoute(route, names);
    }

    std::string WriteRoute(Route const& route, RouteNames const& names)
    {
        std::ostringstream out;
        out << CommentMarker << " The route of " << route.name << ". The GM command .route wrote this file.\n";
        out << "route " << route.name << " " << route.mapId << "\n";
        if (route.hasSeed)
            out << "seed " << route.seed << "\n";
        for (std::uint32_t boss : route.bosses)
            out << "boss " << boss << Comment(names.entry, boss) << "\n";
        for (RouteQuest const& quest : route.quests)
        {
            out << "quest " << quest.id << Comment(names.quest, quest.id) << "\n";
            for (QuestObjective const& objective : quest.objectives)
            {
                switch (objective.kind)
                {
                    case QuestObjectiveKind::Visit:
                        out << "visit " << objective.point << "\n";
                        break;
                    case QuestObjectiveKind::Kill:
                        out << "kill " << objective.entry << " " << objective.count
                            << Comment(names.entry, objective.entry) << "\n";
                        break;
                    case QuestObjectiveKind::Item:
                        out << "item " << objective.item << " " << objective.count;
                        for (std::uint32_t entry : objective.entries)
                            out << " " << entry;
                        out << Comment(names.item, objective.item) << "\n";
                        break;
                }
            }
        }
        for (RouteStep const& step : route.steps)
        {
            if (step.kind == RouteStepKind::Point)
            {
                RoutePoint const& point = route.points[step.index];
                for (std::size_t i = 0; i < point.places.size(); ++i)
                {
                    RoutePlace const& place = point.places[i];
                    if (i == 0)
                        out << "point " << point.name << " ";
                    else
                        out << "alt ";
                    out << FormatPosition(place.position) << " " << FormatFloat(place.radius);
                    if (i == 0)
                        out << QuestTag(point.quest);
                    out << "\n";
                }
            }
            else
            {
                RoutePack const& pack = route.packs[step.index];
                out << "pack " << pack.name << " " << FormatPosition(pack.position) << QuestTag(pack.quest) << "\n";
                for (RouteMob const& mob : pack.mobs)
                    out << "mob " << mob.spawnGuid << " " << mob.entry << Comment(names.entry, mob.entry) << "\n";
            }
        }
        return out.str();
    }
}

void Bots::BossList::Add(Route const& route)
{
    for (std::uint32_t entry : route.bosses)
        m_bosses.emplace(route.mapId, entry);
}

bool Bots::BossList::IsBoss(std::uint32_t mapId, std::uint32_t entry) const
{
    return m_bosses.count(std::make_pair(mapId, entry)) != 0;
}
