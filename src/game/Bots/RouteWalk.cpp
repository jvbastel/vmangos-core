/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// The route walk: a GM walks a route file step by step, to check it in the
// game. Each command shows the current step, the next point, and the next
// pack, and puts waypoint markers on the places of the point.
//   .route walk <route> [character]  Loads the route file, at its first step.
//   .route next                      Goes to the next step and shows it.
//   .route back                      Goes to the step before and shows it.
//   .route go                        Teleports the character to the step.
// A wrong pack is fixed with the recorder: .route start <route>, then
// .route add mob <pack> and .route remove mob.

#include "Chat.h"
#include "Map.h"
#include "Movement/MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Route.h"
#include "RouteFiles.h"
#include "WorldSession.h"

#include <cmath>
#include <map>
#include <sstream>

namespace
{
    // How long the waypoint markers of a step stay.
    uint32 const MarkerMs = 120 * IN_MILLISECONDS;

    struct Walk
    {
        Bots::Route route;
        std::string path;
        ObjectGuid character;
        std::size_t step = 0;
    };

    // By account id of the GM. 0 is the console.
    std::map<uint32, Walk> walks;

    uint32 Owner(ChatHandler& handler)
    {
        return handler.GetSession() ? handler.GetSession()->GetAccountId() : 0;
    }

    bool Fail(ChatHandler& handler, std::string const& line)
    {
        handler.SendSysMessage(line.c_str());
        return false;
    }

    float DistanceTo(Player const* character, Bots::Position const& p)
    {
        float const dx = character->GetPositionX() - p.x;
        float const dy = character->GetPositionY() - p.y;
        float const dz = character->GetPositionZ() - p.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    std::string QuestNote(uint32 quest)
    {
        return quest ? " For quest " + std::to_string(quest) + "." : "";
    }

    void ShowPoint(ChatHandler& handler, Player* character, Walk const& walk, std::size_t step, char const* label)
    {
        Bots::RoutePoint const& point = walk.route.points[walk.route.steps[step].index];
        Bots::Position const& first = point.places[0].position;
        handler.PSendSysMessage("%s: step %u, point %s at %.2f %.2f %.2f, radius %.1f, %.1f yards away, %u places.%s",
            label, uint32(step + 1), point.name.c_str(), first.x, first.y, first.z, point.places[0].radius,
            DistanceTo(character, first), uint32(point.places.size()), QuestNote(point.quest).c_str());
        for (std::size_t k = 1; k < point.places.size(); ++k)
        {
            Bots::Position const& place = point.places[k].position;
            handler.PSendSysMessage("  place %u at %.2f %.2f %.2f, %.1f yards away.", uint32(k + 1), place.x, place.y,
                place.z, DistanceTo(character, place));
        }
    }

    void ShowPack(ChatHandler& handler, Player* character, Walk const& walk, std::size_t step, char const* label)
    {
        Bots::RoutePack const& pack = walk.route.packs[walk.route.steps[step].index];
        handler.PSendSysMessage("%s: step %u, pack %s with %u mobs at %.2f %.2f %.2f, %.1f yards away.%s", label,
            uint32(step + 1), pack.name.c_str(), uint32(pack.mobs.size()), pack.position.x, pack.position.y,
            pack.position.z, DistanceTo(character, pack.position), QuestNote(pack.quest).c_str());
        for (Bots::RouteMob const& mob : pack.mobs)
        {
            CreatureData const* data = sObjectMgr.GetCreatureData(mob.spawnGuid);
            Creature* creature = data ? character->GetMap()->GetCreature(data->GetObjectGuid(mob.spawnGuid)) : nullptr;
            std::string state = "not in this map";
            if (creature)
            {
                std::ostringstream text;
                text.precision(1);
                text << std::fixed << (creature->IsAlive() ? "alive" : "dead") << ", "
                     << character->GetDistance(creature) << " yards away";
                state = text.str();
            }
            handler.PSendSysMessage("  %s %u (%u): %s.", RouteFiles::EntryName(mob.entry).c_str(), mob.spawnGuid,
                mob.entry, state.c_str());
        }
    }

    // The first step of that kind at or after `from`, or the step count.
    std::size_t NextOf(Walk const& walk, std::size_t from, Bots::RouteStepKind kind)
    {
        for (std::size_t step = from; step < walk.route.steps.size(); ++step)
            if (walk.route.steps[step].kind == kind)
                return step;
        return walk.route.steps.size();
    }

    void Mark(Player* character, Walk const& walk, std::size_t step)
    {
        if (step >= walk.route.steps.size() || walk.route.steps[step].kind != Bots::RouteStepKind::Point)
            return;
        for (Bots::RoutePlace const& place : walk.route.points[walk.route.steps[step].index].places)
            if (Creature* marker = character->SummonCreature(VISUAL_WAYPOINT, place.position.x, place.position.y,
                    place.position.z, 0.0f, TEMPSUMMON_TIMED_DESPAWN, MarkerMs))
            {
                marker->SetFly(true);
                marker->SendHeartBeat();
            }
    }

    // Shows the current step, the next point, and the next pack.
    bool Show(ChatHandler& handler, Walk& walk)
    {
        Player* character = ObjectAccessor::FindPlayer(walk.character);
        if (!character || !character->IsInWorld())
            return Fail(handler, "The walking character is not in the world.");
        if (character->GetMapId() != walk.route.mapId)
            return Fail(handler, "The walking character is on map " + std::to_string(character->GetMapId()) +
                ", but the route is on map " + std::to_string(walk.route.mapId) + ".");
        std::size_t const count = walk.route.steps.size();
        if (walk.step >= count)
        {
            handler.PSendSysMessage("The end of the route %s: %u steps.", walk.route.name.c_str(), uint32(count));
            return true;
        }
        handler.PSendSysMessage("Route %s, step %u of %u.", walk.route.name.c_str(), uint32(walk.step + 1),
            uint32(count));
        std::size_t const point = NextOf(walk, walk.step, Bots::RouteStepKind::Point);
        std::size_t const pack = NextOf(walk, walk.step, Bots::RouteStepKind::Pack);
        if (point < count)
            ShowPoint(handler, character, walk, point, point == walk.step ? "Now" : "Next point");
        else
            handler.SendSysMessage("Next point: none.");
        if (pack < count)
            ShowPack(handler, character, walk, pack, pack == walk.step ? "Now" : "Next pack");
        else
            handler.SendSysMessage("Next pack: none.");
        Mark(character, walk, point);
        return true;
    }

    Walk* Current(ChatHandler& handler)
    {
        auto it = walks.find(Owner(handler));
        if (it == walks.end())
        {
            Fail(handler, "No route walk. Use .route walk <route> first.");
            return nullptr;
        }
        return &it->second;
    }

    std::vector<std::string> Words(char const* args)
    {
        std::vector<std::string> words;
        std::istringstream stream(args ? args : "");
        std::string word;
        while (stream >> word)
            words.push_back(word);
        return words;
    }
}

namespace
{
bool RunWalk(ChatHandler& handler, char* args)
{
    std::vector<std::string> words = Words(args);
    if (words.empty() || words.size() > 2)
        return Fail(handler, "Usage: .route walk <route> [character]");
    if (!Bots::IsValidRouteName(words[0]))
        return Fail(handler, "The route name must have only a-z, 0-9, and _.");
    Player* character = nullptr;
    if (words.size() == 2)
    {
        std::string name = words[1];
        if (!normalizePlayerName(name) || !(character = ObjectAccessor::FindPlayerByName(name.c_str())))
            return Fail(handler, "No character " + words[1] + " is online.");
    }
    else if (handler.GetSession())
        character = handler.GetSession()->GetPlayer();
    if (!character || !character->IsInWorld())
        return Fail(handler, "On the console, name an online character: .route walk <route> <character>");

    Walk walk;
    walk.path = RouteFiles::PathOf(words[0]);
    walk.character = character->GetObjectGuid();
    std::string text;
    if (!RouteFiles::Read(walk.path, text))
        return Fail(handler, "Cannot read " + walk.path + ".");
    Bots::RouteResult parsed = Bots::ParseRoute(text);
    if (!parsed.ok)
        return Fail(handler, walk.path + ": " + parsed.error);
    walk.route = parsed.route;
    walks[Owner(handler)] = walk;
    return Show(handler, walks[Owner(handler)]);
}
}

bool ChatHandler::HandleRouteWalkCommand(char* args)
{
    if (RunWalk(*this, args))
        return true;
    SetSentErrorMessage(true);
    return false;
}
namespace
{
bool RunNext(ChatHandler& handler, char* )
{
    Walk* walk = Current(handler);
    if (!walk)
        return false;
    if (walk->step < walk->route.steps.size())
        ++walk->step;
    return Show(handler, *walk);
}
}

bool ChatHandler::HandleRouteNextCommand(char* args)
{
    if (RunNext(*this, args))
        return true;
    SetSentErrorMessage(true);
    return false;
}
namespace
{
bool RunBack(ChatHandler& handler, char* )
{
    Walk* walk = Current(handler);
    if (!walk)
        return false;
    if (walk->step > 0)
        --walk->step;
    return Show(handler, *walk);
}
}

bool ChatHandler::HandleRouteBackCommand(char* args)
{
    if (RunBack(*this, args))
        return true;
    SetSentErrorMessage(true);
    return false;
}
namespace
{
bool RunGo(ChatHandler& handler, char* )
{
    Walk* walk = Current(handler);
    if (!walk)
        return false;
    Player* character = ObjectAccessor::FindPlayer(walk->character);
    if (!character || !character->IsInWorld() || character->GetMapId() != walk->route.mapId)
        return Fail(handler, "The walking character is not in the world on the route's map.");
    if (walk->step >= walk->route.steps.size())
        return Fail(handler, "The walk is at the end of the route. Use .route back.");
    Bots::RouteStep const& step = walk->route.steps[walk->step];
    Bots::Position const target = step.kind == Bots::RouteStepKind::Point
        ? walk->route.points[step.index].places[0].position
        : walk->route.packs[step.index].position;
    character->NearTeleportTo(target.x, target.y, target.z + 0.5f, character->GetOrientation());
    handler.PSendSysMessage("%s goes to step %u.", character->GetName(), uint32(walk->step + 1));
    return true;
}
}

bool ChatHandler::HandleRouteGoCommand(char* args)
{
    if (RunGo(*this, args))
        return true;
    SetSentErrorMessage(true);
    return false;
}
