/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// The GM route recorder. A GM walks a dungeon and records its route file
// with these commands. Nobody types coordinates.
//   .route start <route> [character]  Record <Bots.DataDir>/routes/<route>.txt.
//                                     An existing file is read and extended.
//   .route add point [name] [radius]  The character's position, radius 2.
//   .route add alt [radius]           One more place for the last point.
//   .route add pack [name]            The selected creature and its creature
//                                     group. With no selection, the nearest
//                                     hostile creature in 20 yards.
//   .route add mob [pack]             The selected creature, to the last pack
//                                     or to the pack of that name.
//   .route remove mob                 Takes the selected creature out of its
//                                     pack, to fix a generated pack.
//   .route add boss                   The selected creature's entry, to the
//                                     boss list.
//   .route undo                       Removes the last point or pack.
//   .route show                       The route so far.
//   .route stop                       Stops recording. The file stays.
// Each change writes the file at once. In the game, the recorder uses the
// GM's own character. On the console and SOAP, `.route start` names an
// online character (a bot, for example), and the console has one recording.

#include "BotMgr.h"
#include "Chat.h"
#include "CreatureGroups.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ItemPrototype.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "Route.h"
#include "RouteFiles.h"
#include "WorldSession.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

std::string RouteFiles::EntryName(uint32 entry)
{
    CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
    return info ? info->name : "";
}

Bots::RouteNames RouteFiles::Names()
{
    Bots::RouteNames names;
    names.entry = EntryName;
    names.quest = [](uint32 id) {
        Quest const* quest = sObjectMgr.GetQuestTemplate(id);
        return quest ? quest->GetTitle() : std::string();
    };
    names.item = [](uint32 id) {
        ItemPrototype const* item = sObjectMgr.GetItemPrototype(id);
        return item ? std::string(item->Name1) : std::string();
    };
    return names;
}

std::string RouteFiles::PathOf(std::string const& name)
{
    std::string dir = sBotMgr.GetSettings().dataDir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
        dir += '/';
    return dir + "routes/" + name + ".txt";
}

bool RouteFiles::Read(std::string const& path, std::string& text)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    std::ostringstream content;
    content << file.rdbuf();
    text = content.str();
    return true;
}

bool RouteFiles::Write(std::string const& path, std::string const& text)
{
    std::string const temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;
        file << text;
        if (!file.flush())
            return false;
    }
    std::remove(path.c_str());
    return std::rename(temporary.c_str(), path.c_str()) == 0;
}

namespace
{
    float const DefaultRadius = 2.0f;
    float const NearestPackRange = 20.0f;

    struct Recording
    {
        Bots::Route route;
        std::string path;
        ObjectGuid character;
    };

    // By account id of the GM. 0 is the console.
    std::map<uint32, Recording> recordings;

    std::vector<std::string> Words(char const* args)
    {
        std::vector<std::string> words;
        std::istringstream stream(args ? args : "");
        std::string word;
        while (stream >> word)
            words.push_back(word);
        return words;
    }

    bool IsRadius(std::string const& word, float& radius)
    {
        char* end = nullptr;
        float const value = std::strtof(word.c_str(), &end);
        if (word.empty() || *end != '\0')
            return false;
        radius = value;
        return true;
    }

    Bots::Position PositionOf(WorldObject const* object)
    {
        return {object->GetPositionX(), object->GetPositionY(), object->GetPositionZ()};
    }

    Bots::Position SpawnPositionOf(Creature const* creature)
    {
        Bots::Position position{};
        creature->GetRespawnCoord(position.x, position.y, position.z);
        return position;
    }

    // The creature that the character has selected, or nullptr. Only
    // creatures of the world database count: no pets, no summons.
    Creature* SelectedSpawn(Player* character)
    {
        Creature* creature = character->GetMap()->GetAnyTypeCreature(character->GetSelectionGuid());
        if (!creature || creature->IsPet() || !creature->GetDBTableGUIDLow())
            return nullptr;
        return creature;
    }

    Creature* PackLeader(Player* character)
    {
        if (Creature* selected = SelectedSpawn(character))
            return selected;
        Unit* nearest = character->SelectNearestTarget(NearestPackRange);
        Creature* creature = nearest ? nearest->ToCreature() : nullptr;
        if (!creature || creature->IsPet() || !creature->GetDBTableGUIDLow())
            return nullptr;
        return creature;
    }

    // The creature and the other creatures of its creature group.
    std::vector<Creature*> PackOf(Creature* first)
    {
        std::vector<Creature*> creatures{first};
        auto add = [&creatures](Creature* creature) {
            if (!creature || !creature->GetDBTableGUIDLow())
                return;
            for (Creature* known : creatures)
                if (known == creature)
                    return;
            creatures.push_back(creature);
        };
        if (CreatureGroup* group = first->GetCreatureGroup())
        {
            add(first->GetMap()->GetCreature(group->GetOriginalLeaderGuid()));
            group->DoForAllMembers(first->GetMap(), add);
        }
        return creatures;
    }

    Bots::RouteMob MobOf(Creature const* creature)
    {
        return {creature->GetDBTableGUIDLow(), creature->GetEntry()};
    }
}

class RouteRecorderCommand
{
    public:
        explicit RouteRecorderCommand(ChatHandler& handler) : m_handler(handler) {}

        uint32 Owner() const
        {
            WorldSession* session = m_handler.GetSession();
            return session ? session->GetAccountId() : 0;
        }

        // The recording of this GM, or nullptr with a message.
        Recording* Current()
        {
            auto it = recordings.find(Owner());
            if (it == recordings.end())
            {
                Fail("No route is being recorded. Use .route start <route> first.");
                return nullptr;
            }
            return &it->second;
        }

        // The character of the recording, in the world and on the route's map.
        Player* Character(Recording const& recording)
        {
            Player* character = ObjectAccessor::FindPlayer(recording.character);
            if (!character || !character->IsInWorld())
            {
                Fail("The recording character is not in the world.");
                return nullptr;
            }
            if (character->GetMapId() != recording.route.mapId)
            {
                Fail("The recording character is on map %u, but the route is on map %u.",
                    character->GetMapId(), recording.route.mapId);
                return nullptr;
            }
            return character;
        }

        // Writes the file after a change. On an error, the route goes back.
        bool Save(Recording& recording, Bots::Route const& before)
        {
            if (RouteFiles::Write(recording.path, Bots::WriteRoute(recording.route, RouteFiles::Names())))
                return true;
            recording.route = before;
            Fail("Cannot write %s. Does the routes folder exist?", recording.path.c_str());
            return false;
        }

        bool Done(std::string const& error)
        {
            if (!error.empty())
                return Fail("%s", error.c_str());
            return true;
        }

        bool Fail(char const* format, ...) ATTR_PRINTF(2, 3)
        {
            char text[1024];
            va_list ap;
            va_start(ap, format);
            vsnprintf(text, sizeof(text), format, ap);
            va_end(ap);
            m_error = text;
            return false;
        }

        std::string const& Error() const { return m_error; }

    private:
        ChatHandler& m_handler;
        std::string m_error;
};

namespace
{
    bool RouteStart(ChatHandler& handler, RouteRecorderCommand& command, char* args)
    {
        std::vector<std::string> words = Words(args);
        if (words.empty() || words.size() > 2)
            return command.Fail("Usage: .route start <route> [character]");
        if (!Bots::IsValidRouteName(words[0]))
            return command.Fail("The route name must have only a-z, 0-9, and _.");

        Player* character = nullptr;
        if (words.size() == 2)
        {
            std::string name = words[1];
            if (!normalizePlayerName(name) || !(character = ObjectAccessor::FindPlayerByName(name.c_str())))
                return command.Fail("No character %s is online.", words[1].c_str());
        }
        else if (handler.GetSession())
            character = handler.GetSession()->GetPlayer();
        if (!character || !character->IsInWorld())
            return command.Fail("On the console, name an online character: .route start <route> <character>");

        Recording recording;
        recording.path = RouteFiles::PathOf(words[0]);
        recording.character = character->GetObjectGuid();
        std::string text;
        if (RouteFiles::Read(recording.path, text))
        {
            Bots::RouteResult parsed = Bots::ParseRoute(text);
            if (!parsed.ok)
                return command.Fail("%s: %s", recording.path.c_str(), parsed.error.c_str());
            if (parsed.route.name != words[0])
                return command.Fail("%s names the route %s.", recording.path.c_str(), parsed.route.name.c_str());
            recording.route = parsed.route;
            if (recording.route.mapId != character->GetMapId())
                return command.Fail("The route %s is on map %u, but %s is on map %u.", words[0].c_str(),
                    recording.route.mapId, character->GetName(), character->GetMapId());
            handler.PSendSysMessage("Route %s continues: %u steps, %u bosses. File %s.", words[0].c_str(),
                uint32(recording.route.steps.size()), uint32(recording.route.bosses.size()), recording.path.c_str());
        }
        else
        {
            recording.route.name = words[0];
            recording.route.mapId = character->GetMapId();
            if (!RouteFiles::Write(recording.path, Bots::WriteRoute(recording.route, RouteFiles::Names())))
                return command.Fail("Cannot write %s. Does the routes folder exist?", recording.path.c_str());
            handler.PSendSysMessage("New route %s on map %u. File %s.", words[0].c_str(), recording.route.mapId,
                recording.path.c_str());
        }
        recordings[command.Owner()] = recording;
        handler.PSendSysMessage("%s records the route.", character->GetName());
        return true;
    }

    bool RouteAddPoint(ChatHandler& handler, RouteRecorderCommand& command, char* args)
    {
        Recording* recording = command.Current();
        Player* character = recording ? command.Character(*recording) : nullptr;
        if (!character)
            return false;

        std::string name;
        float radius = DefaultRadius;
        for (std::string const& word : Words(args))
        {
            float number = 0.0f;
            if (IsRadius(word, number))
                radius = number;
            else if (name.empty())
                name = word;
            else
                return command.Fail("Usage: .route add point [name] [radius]");
        }

        Bots::Route const before = recording->route;
        if (!command.Done(Bots::AddPoint(recording->route, name, {PositionOf(character), radius})) ||
            !command.Save(*recording, before))
            return false;
        Bots::RoutePoint const& point = recording->route.points.back();
        handler.PSendSysMessage("Point %s at %.2f %.2f %.2f, radius %.1f.", point.name.c_str(), point.places[0].position.x,
            point.places[0].position.y, point.places[0].position.z, radius);
        return true;
    }

    bool RouteAddAlt(ChatHandler& handler, RouteRecorderCommand& command, char* args)
    {
        Recording* recording = command.Current();
        Player* character = recording ? command.Character(*recording) : nullptr;
        if (!character)
            return false;

        std::vector<std::string> words = Words(args);
        float radius = DefaultRadius;
        if (words.size() > 1 || (words.size() == 1 && !IsRadius(words[0], radius)))
            return command.Fail("Usage: .route add alt [radius]");

        Bots::Route const before = recording->route;
        if (!command.Done(Bots::AddAlternative(recording->route, {PositionOf(character), radius})) ||
            !command.Save(*recording, before))
            return false;
        Bots::RoutePoint const& point = recording->route.points.back();
        handler.PSendSysMessage("Point %s now has %u places.", point.name.c_str(), uint32(point.places.size()));
        return true;
    }

    bool RouteAddPack(ChatHandler& handler, RouteRecorderCommand& command, char* args)
    {
        Recording* recording = command.Current();
        Player* character = recording ? command.Character(*recording) : nullptr;
        if (!character)
            return false;

        std::vector<std::string> words = Words(args);
        if (words.size() > 1)
            return command.Fail("Usage: .route add pack [name]");
        Creature* leader = PackLeader(character);
        if (!leader)
            return command.Fail("Select a creature of the pack. No hostile creature is within %.0f yards.",
                NearestPackRange);

        std::vector<Bots::RouteMob> mobs;
        std::ostringstream names;
        for (Creature* creature : PackOf(leader))
        {
            mobs.push_back(MobOf(creature));
            names << (mobs.size() > 1 ? ", " : "") << creature->GetName() << " " << creature->GetDBTableGUIDLow();
        }

        Bots::Route const before = recording->route;
        if (!command.Done(Bots::AddPack(recording->route, words.empty() ? "" : words[0], SpawnPositionOf(leader),
                mobs)) || !command.Save(*recording, before))
            return false;
        handler.PSendSysMessage("Pack %s with %u mobs: %s. Add a mob with .route add mob.",
            recording->route.packs.back().name.c_str(), uint32(mobs.size()), names.str().c_str());
        return true;
    }

    bool RouteAddMob(ChatHandler& handler, RouteRecorderCommand& command, char* args)
    {
        Recording* recording = command.Current();
        Player* character = recording ? command.Character(*recording) : nullptr;
        if (!character)
            return false;
        std::vector<std::string> words = Words(args);
        if (words.size() > 1)
            return command.Fail("Usage: .route add mob [pack]");
        Creature* creature = SelectedSpawn(character);
        if (!creature)
            return command.Fail("Select a creature of the world database (no pet, no summon).");

        Bots::Route const before = recording->route;
        std::string const error = words.empty() ? Bots::AddMob(recording->route, MobOf(creature))
                                                : Bots::AddMobTo(recording->route, words[0], MobOf(creature));
        if (!command.Done(error) || !command.Save(*recording, before))
            return false;
        handler.PSendSysMessage("Pack %s: added %s %u.",
            words.empty() ? recording->route.packs.back().name.c_str() : words[0].c_str(), creature->GetName(),
            creature->GetDBTableGUIDLow());
        return true;
    }

    bool RouteRemoveMob(ChatHandler& handler, RouteRecorderCommand& command, char* /*args*/)
    {
        Recording* recording = command.Current();
        Player* character = recording ? command.Character(*recording) : nullptr;
        if (!character)
            return false;
        Creature* creature = SelectedSpawn(character);
        if (!creature)
            return command.Fail("Select the creature to take out of its pack.");

        Bots::Route const before = recording->route;
        if (!command.Done(Bots::RemoveMob(recording->route, creature->GetDBTableGUIDLow())) ||
            !command.Save(*recording, before))
            return false;
        handler.PSendSysMessage("Removed %s %u from its pack.", creature->GetName(), creature->GetDBTableGUIDLow());
        return true;
    }

    bool RouteAddBoss(ChatHandler& handler, RouteRecorderCommand& command, char* /*args*/)
    {
        Recording* recording = command.Current();
        Player* character = recording ? command.Character(*recording) : nullptr;
        if (!character)
            return false;
        Creature* creature = SelectedSpawn(character);
        if (!creature)
            return command.Fail("Select the boss.");

        Bots::Route const before = recording->route;
        if (!command.Done(Bots::AddBoss(recording->route, creature->GetEntry())) || !command.Save(*recording, before))
            return false;
        handler.PSendSysMessage("Boss %s (%u) is in the boss list.", creature->GetName(), creature->GetEntry());
        return true;
    }

    bool RouteUndo(ChatHandler& handler, RouteRecorderCommand& command, char* /*args*/)
    {
        Recording* recording = command.Current();
        if (!recording)
            return false;
        Bots::Route const before = recording->route;
        if (!command.Done(Bots::RemoveLastStep(recording->route)) || !command.Save(*recording, before))
            return false;
        handler.PSendSysMessage("Removed the last step. The route has %u steps.", uint32(recording->route.steps.size()));
        return true;
    }

    bool RouteShow(ChatHandler& handler, RouteRecorderCommand& command, char* /*args*/)
    {
        Recording* recording = command.Current();
        if (!recording)
            return false;
        std::istringstream lines(Bots::WriteRoute(recording->route, RouteFiles::Names()));
        std::string line;
        while (std::getline(lines, line))
            handler.SendSysMessage(line.c_str());
        handler.PSendSysMessage("File %s.", recording->path.c_str());
        return true;
    }

    bool RouteStop(ChatHandler& handler, RouteRecorderCommand& command, char* /*args*/)
    {
        Recording* recording = command.Current();
        if (!recording)
            return false;
        handler.PSendSysMessage("Stopped recording %s: %u steps, %u bosses in %s.", recording->route.name.c_str(),
            uint32(recording->route.steps.size()), uint32(recording->route.bosses.size()), recording->path.c_str());
        recordings.erase(command.Owner());
        return true;
    }
}

// The handlers of the command table. A failed command shows its error line.

bool ChatHandler::HandleRouteStartCommand(char* args)
{
    RouteRecorderCommand command(*this);
    if (RouteStart(*this, command, args))
        return true;
    SendSysMessage(command.Error().c_str());
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteAddPointCommand(char* args)
{
    RouteRecorderCommand command(*this);
    if (RouteAddPoint(*this, command, args))
        return true;
    SendSysMessage(command.Error().c_str());
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteAddAltCommand(char* args)
{
    RouteRecorderCommand command(*this);
    if (RouteAddAlt(*this, command, args))
        return true;
    SendSysMessage(command.Error().c_str());
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteAddPackCommand(char* args)
{
    RouteRecorderCommand command(*this);
    if (RouteAddPack(*this, command, args))
        return true;
    SendSysMessage(command.Error().c_str());
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteAddMobCommand(char* args)
{
    RouteRecorderCommand command(*this);
    if (RouteAddMob(*this, command, args))
        return true;
    SendSysMessage(command.Error().c_str());
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteRemoveMobCommand(char* args)
{
    RouteRecorderCommand command(*this);
    if (RouteRemoveMob(*this, command, args))
        return true;
    SendSysMessage(command.Error().c_str());
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteAddBossCommand(char* args)
{
    RouteRecorderCommand command(*this);
    if (RouteAddBoss(*this, command, args))
        return true;
    SendSysMessage(command.Error().c_str());
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteUndoCommand(char* args)
{
    RouteRecorderCommand command(*this);
    if (RouteUndo(*this, command, args))
        return true;
    SendSysMessage(command.Error().c_str());
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteShowCommand(char* args)
{
    RouteRecorderCommand command(*this);
    if (RouteShow(*this, command, args))
        return true;
    SendSysMessage(command.Error().c_str());
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteStopCommand(char* args)
{
    RouteRecorderCommand command(*this);
    if (RouteStop(*this, command, args))
        return true;
    SendSysMessage(command.Error().c_str());
    SetSentErrorMessage(true);
    return false;
}
