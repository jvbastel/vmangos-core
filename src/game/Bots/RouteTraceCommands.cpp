/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// The GM trace of a route (bots/RouteTrace.h). The GM walks the dungeon one
// time, and the server records the path to <Bots.DataDir>/routes/<route>.trace.txt.
// `.route build` (RouteGenerate.cpp) then makes the route file around it.
//   .route trace start <route> [character] [step=4] [floor=3] [jump=20]
//                              Starts a trace. A trace file that exists gets
//                              a later walk at its end.
//   .route trace stop          Stops the trace and writes it.
//   .route trace undo [yards]  Removes the end of the trace (10 yards).
//   .route trace show          The state of the trace.
//   .route trace mark [note]   A checkpoint where the character stands.
//   .route trace pause         No points until .route trace resume.
//   .route trace resume        Points again, from where the character stands.
// Each command answers with one line in a fixed form, which the RouteRecorder
// addon (wowai/addons/RouteRecorder) reads:
//   [route] trace on ragefire_chasm points=212 marks=9 length=640y
// An error is `[route] trace error: ...`, a warning `[route] trace warning: ...`.
// The console and SOAP name an online character, as `.route start` does.

#include "Chat.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RouteFiles.h"
#include "RouteTrace.h"
#include "RouteTraces.h"
#include "World.h"
#include "WorldSession.h"

#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>

namespace
{
    // The recorder looks this often, in milliseconds.
    uint32 const LookInterval = 250;
    // The file is written after this many new points, and at each command.
    uint32 const SaveEvery = 10;

    struct TraceRecording
    {
        std::unique_ptr<Bots::TraceRecorder> recorder;
        std::string path;
        ObjectGuid character;
        uint32 unsaved = 0;
    };

    // By account id of the GM. 0 is the console.
    std::map<uint32, TraceRecording> traces;
    uint32 sinceLook = 0;

    std::vector<std::string> Words(char const* args)
    {
        std::vector<std::string> words;
        std::istringstream stream(args ? args : "");
        std::string word;
        while (stream >> word)
            words.push_back(word);
        return words;
    }

    bool ReadYards(std::string const& text, float& value)
    {
        char* end = nullptr;
        float const number = std::strtof(text.c_str(), &end);
        if (text.empty() || *end != '\0' || !(number > 0.0f) || number > 1000.0f)
            return false;
        value = number;
        return true;
    }

    Bots::Position PositionOf(Player const* player)
    {
        return {player->GetPositionX(), player->GetPositionY(), player->GetPositionZ()};
    }

    Bots::TraceMotion MotionOf(Player const* player, uint32 mapId)
    {
        if (!player->IsInWorld() || player->GetMapId() != mapId || player->IsBeingTeleported())
            return Bots::TraceMotion::Away;
        if (player->IsFlying() || player->IsTaxiFlying())
            return Bots::TraceMotion::Flying;
        return Bots::TraceMotion::Ground;
    }

    bool Save(TraceRecording& recording)
    {
        recording.unsaved = 0;
        return RouteFiles::Write(recording.path, Bots::WriteTrace(recording.recorder->GetTrace()));
    }

    std::string Status(TraceRecording const& recording, Bots::TraceState state)
    {
        return Bots::TraceStatusLine(recording.recorder->GetTrace(), state);
    }

    // A warning of the recorder goes to the GM, and to the log for the
    // console and the checks.
    void Warn(uint32 owner, std::string const& text)
    {
        std::string const line = "[route] trace warning: " + text;
        sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] %s", line.c_str());
        if (WorldSession* session = owner ? sWorld.FindSession(owner) : nullptr)
            ChatHandler(session).SendSysMessage(line.c_str());
    }

    class TraceCommand
    {
    public:
        explicit TraceCommand(ChatHandler& handler) : m_handler(handler) {}

        uint32 Owner() const { return m_handler.GetSession() ? m_handler.GetSession()->GetAccountId() : 0; }

        // The trace of this GM, or nullptr with an error.
        TraceRecording* Current()
        {
            auto it = traces.find(Owner());
            if (it == traces.end())
            {
                Fail("no trace is on. Use .route trace start <route> first.");
                return nullptr;
            }
            return &it->second;
        }

        // The character of the trace, on the map of the trace.
        Player* Character(TraceRecording const& recording)
        {
            Player* character = ObjectAccessor::FindPlayer(recording.character);
            if (!character || !character->IsInWorld())
            {
                Fail("the trace character is not in the world.");
                return nullptr;
            }
            if (character->GetMapId() != recording.recorder->GetTrace().mapId)
            {
                Fail("the trace is on map " + std::to_string(recording.recorder->GetTrace().mapId) + ", but " +
                    character->GetName() + " is on map " + std::to_string(character->GetMapId()) + ".");
                return nullptr;
            }
            return character;
        }

        bool Fail(std::string const& text)
        {
            m_handler.PSendSysMessage("[route] trace error: %s", text.c_str());
            return false;
        }

        // The status line, a warning, and the file.
        bool Answer(TraceRecording& recording, Bots::TraceReply const& reply, Bots::TraceState state)
        {
            if (!reply.ok)
                return Fail(reply.text + ".");
            if (!Save(recording))
                return Fail("cannot write " + recording.path + ". Does the routes folder exist?");
            m_handler.SendSysMessage(Status(recording, state).c_str());
            if (!reply.text.empty())
                m_handler.PSendSysMessage("[route] trace warning: %s", reply.text.c_str());
            return true;
        }

        ChatHandler& Handler() { return m_handler; }

    private:
        ChatHandler& m_handler;
    };

    bool TraceStart(TraceCommand& command, char* args)
    {
        std::vector<std::string> names;
        Bots::TraceSettings settings;
        for (std::string const& word : Words(args))
        {
            std::size_t const equals = word.find('=');
            if (equals == std::string::npos)
            {
                names.push_back(word);
                continue;
            }
            std::string const key = word.substr(0, equals);
            float* value = key == "step" ? &settings.step : key == "floor" ? &settings.floorChange
                : key == "jump" ? &settings.jump : nullptr;
            if (!value)
                return command.Fail("unknown setting " + key + ". Use step, floor, or jump.");
            if (!ReadYards(word.substr(equals + 1), *value))
                return command.Fail("the setting " + key + " needs a number of yards above 0.");
        }
        if (names.empty() || names.size() > 2)
            return command.Fail("use .route trace start <route> [character] [step=4] [floor=3] [jump=20]");
        if (!Bots::IsValidRouteName(names[0]))
            return command.Fail("the route name must have only a-z, 0-9, and _.");
        if (traces.count(command.Owner()))
            return command.Fail("a trace is on already. Stop it with .route trace stop first.");

        ChatHandler& handler = command.Handler();
        Player* character = nullptr;
        if (names.size() == 2)
        {
            std::string name = names[1];
            if (!normalizePlayerName(name) || !(character = ObjectAccessor::FindPlayerByName(name.c_str())))
                return command.Fail("no character " + names[1] + " is online.");
        }
        else if (handler.GetSession())
            character = handler.GetSession()->GetPlayer();
        if (!character || !character->IsInWorld())
            return command.Fail("on the console, name an online character: .route trace start <route> <character>");

        Bots::Trace trace;
        trace.name = names[0];
        trace.mapId = character->GetMapId();
        std::string const path = RouteFiles::TracePathOf(names[0]);
        for (auto const& other : traces)
            if (other.second.path == path)
                return command.Fail("another GM records the trace " + names[0] + " now.");
        std::string text;
        if (RouteFiles::Read(path, text))
        {
            Bots::TraceResult parsed = Bots::ParseTrace(text);
            if (!parsed.ok)
                return command.Fail(path + ": " + parsed.error);
            if (parsed.trace.name != names[0])
                return command.Fail(path + " names the route " + parsed.trace.name + ".");
            if (parsed.trace.mapId != character->GetMapId())
                return command.Fail("the trace " + names[0] + " is on map " + std::to_string(parsed.trace.mapId) +
                    ", but " + character->GetName() + " is on map " + std::to_string(character->GetMapId()) + ".");
            trace = parsed.trace;
        }

        TraceRecording& recording = traces[command.Owner()];
        recording.recorder.reset(new Bots::TraceRecorder(trace, settings));
        recording.path = path;
        recording.character = character->GetObjectGuid();
        // The first point is where the character stands now.
        recording.recorder->Look(PositionOf(character), MotionOf(character, trace.mapId));
        if (!Save(recording))
        {
            traces.erase(command.Owner());
            return command.Fail("cannot write " + path + ". Does the routes folder exist?");
        }
        handler.SendSysMessage(Status(recording, Bots::TraceState::On).c_str());
        sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] %s traces %s to %s (step %.1f)", character->GetName(),
            names[0].c_str(), path.c_str(), settings.step);
        return true;
    }

    bool TraceStop(TraceCommand& command, char* /*args*/)
    {
        TraceRecording* recording = command.Current();
        if (!recording)
            return false;
        if (!Save(*recording))
            return command.Fail("cannot write " + recording->path + ". The trace stays on.");
        command.Handler().SendSysMessage(Status(*recording, Bots::TraceState::Off).c_str());
        sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Bots] %s", Status(*recording, Bots::TraceState::Off).c_str());
        traces.erase(command.Owner());
        return true;
    }

    bool TraceUndo(TraceCommand& command, char* args)
    {
        TraceRecording* recording = command.Current();
        if (!recording)
            return false;
        std::vector<std::string> const words = Words(args);
        float yards = Bots::TraceSettings{}.undo;
        if (words.size() > 1 || (words.size() == 1 && !ReadYards(words[0], yards)))
            return command.Fail("use .route trace undo [yards]");
        Bots::TraceReply const reply = recording->recorder->Undo(yards);
        return command.Answer(*recording, reply, recording->recorder->State());
    }

    bool TraceShow(TraceCommand& command, char* /*args*/)
    {
        TraceRecording* recording = command.Current();
        if (!recording)
            return false;
        command.Handler().SendSysMessage(Status(*recording, recording->recorder->State()).c_str());
        return true;
    }

    bool TraceMark(TraceCommand& command, char* args)
    {
        TraceRecording* recording = command.Current();
        Player* character = recording ? command.Character(*recording) : nullptr;
        if (!character)
            return false;
        Bots::TraceReply const reply = recording->recorder->Mark(PositionOf(character), args ? args : "");
        return command.Answer(*recording, reply, recording->recorder->State());
    }

    bool TracePause(TraceCommand& command, char* /*args*/)
    {
        TraceRecording* recording = command.Current();
        if (!recording)
            return false;
        Bots::TraceReply const reply = recording->recorder->Pause();
        return command.Answer(*recording, reply, recording->recorder->State());
    }

    bool TraceResume(TraceCommand& command, char* /*args*/)
    {
        TraceRecording* recording = command.Current();
        Player* character = recording ? command.Character(*recording) : nullptr;
        if (!character)
            return false;
        Bots::TraceReply const reply = recording->recorder->Resume(PositionOf(character));
        return command.Answer(*recording, reply, recording->recorder->State());
    }

    template <bool (*Run)(TraceCommand&, char*)>
    bool Handle(ChatHandler& handler, char* args)
    {
        TraceCommand command(handler);
        return Run(command, args);
    }
}

void RouteTraces::Update(uint32 diff)
{
    sinceLook += diff;
    if (sinceLook < LookInterval || traces.empty())
        return;
    sinceLook = 0;
    for (auto& pair : traces)
    {
        TraceRecording& recording = pair.second;
        Player* character = ObjectAccessor::FindPlayer(recording.character);
        if (!character)
            continue;
        uint32 const mapId = recording.recorder->GetTrace().mapId;
        Bots::TraceReply const reply = recording.recorder->Look(PositionOf(character), MotionOf(character, mapId));
        if (!reply.text.empty())
            Warn(pair.first, reply.text);
        if (recording.recorder->TakeChanged() && ++recording.unsaved >= SaveEvery && !Save(recording))
            Warn(pair.first, "cannot write " + recording.path);
    }
}

bool ChatHandler::HandleRouteTraceStartCommand(char* args)
{
    if (Handle<TraceStart>(*this, args))
        return true;
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteTraceStopCommand(char* args)
{
    if (Handle<TraceStop>(*this, args))
        return true;
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteTraceUndoCommand(char* args)
{
    if (Handle<TraceUndo>(*this, args))
        return true;
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteTraceShowCommand(char* args)
{
    if (Handle<TraceShow>(*this, args))
        return true;
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteTraceMarkCommand(char* args)
{
    if (Handle<TraceMark>(*this, args))
        return true;
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteTracePauseCommand(char* args)
{
    if (Handle<TracePause>(*this, args))
        return true;
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleRouteTraceResumeCommand(char* args)
{
    if (Handle<TraceResume>(*this, args))
        return true;
    SetSentErrorMessage(true);
    return false;
}
