#include "Route.h"

#include "DataTokenizer.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace Bots
{
    namespace
    {
        bool ReadNumber(std::string const& word, std::uint32_t& value)
        {
            if (word.empty() || word.size() > 10)
                return false;
            for (char c : word)
                if (c < '0' || c > '9')
                    return false;
            unsigned long long const number = std::strtoull(word.c_str(), nullptr, 10);
            if (number > 0xFFFFFFFFull)
                return false;
            value = static_cast<std::uint32_t>(number);
            return true;
        }

        bool ReadFloat(std::string const& word, float& value)
        {
            char* end = nullptr;
            errno = 0;
            float const number = std::strtof(word.c_str(), &end);
            if (word.empty() || *end != '\0' || errno != 0 || !std::isfinite(number))
                return false;
            value = number;
            return true;
        }

        bool ReadPosition(std::vector<std::string> const& words, std::size_t first, Position& position)
        {
            return ReadFloat(words[first], position.x) && ReadFloat(words[first + 1], position.y) &&
                ReadFloat(words[first + 2], position.z);
        }

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
                return "use: point <name> <x> <y> <z> <radius>";
            if (keyword == "alt")
                return "use: alt <x> <y> <z> <radius>";
            if (keyword == "pack")
                return "use: pack <name> <x> <y> <z>";
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

        std::string FormatFloat(float value)
        {
            char text[32];
            std::snprintf(text, sizeof(text), "%.2f", value);
            std::string result = text;
            if (result == "-0.00")
                result = "0.00";
            return result;
        }

        std::string FormatPosition(Position const& position)
        {
            return FormatFloat(position.x) + " " + FormatFloat(position.y) + " " + FormatFloat(position.z);
        }

        std::string Comment(std::function<std::string(std::uint32_t)> const& entryName, std::uint32_t entry)
        {
            std::string const name = entryName ? entryName(entry) : "";
            return name.empty() ? "" : std::string(" ") + CommentMarker + " " + name;
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
            else if (keyword == "boss")
            {
                std::uint32_t entry = 0;
                if (wordCountIs(2))
                    error = ReadNumber(words[1], entry) ? AddBoss(route, entry) : Usage(keyword);
            }
            else if (keyword == "point")
            {
                RoutePlace place{};
                if (wordCountIs(6))
                    error = ReadPosition(words, 2, place.position) && ReadFloat(words[5], place.radius)
                        ? AddPoint(route, words[1], place) : Usage(keyword);
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
                if (wordCountIs(5))
                    error = ReadPosition(words, 2, position) ? AddPack(route, words[1], position, {}) : Usage(keyword);
            }
            else if (keyword == "mob")
            {
                RouteMob mob{};
                if (wordCountIs(3))
                    error = ReadNumber(words[1], mob.spawnGuid) && ReadNumber(words[2], mob.entry)
                        ? AddMob(route, mob) : Usage(keyword);
            }
            else
                error = "unknown word '" + keyword + "'. Use route, boss, point, alt, pack, or mob";

            if (!error.empty())
            {
                result.route = Route();
                result.error = "line " + std::to_string(line.number) + ": " + error;
                return result;
            }
        }

        if (!haveHeader)
        {
            result.error = "the file has no route line";
            return result;
        }
        result.ok = true;
        return result;
    }

    std::string WriteRoute(Route const& route,
        std::function<std::string(std::uint32_t entry)> const& entryName)
    {
        std::ostringstream out;
        out << CommentMarker << " The route of " << route.name << ". The GM command .route wrote this file.\n";
        out << "route " << route.name << " " << route.mapId << "\n";
        for (std::uint32_t boss : route.bosses)
            out << "boss " << boss << Comment(entryName, boss) << "\n";
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
                    out << FormatPosition(place.position) << " " << FormatFloat(place.radius) << "\n";
                }
            }
            else
            {
                RoutePack const& pack = route.packs[step.index];
                out << "pack " << pack.name << " " << FormatPosition(pack.position) << "\n";
                for (RouteMob const& mob : pack.mobs)
                    out << "mob " << mob.spawnGuid << " " << mob.entry << Comment(entryName, mob.entry) << "\n";
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
