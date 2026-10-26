#include "RouteTrace.h"
#include "BotGeometry.h"
#include "DataTokenizer.h"
#include "RouteLine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>

namespace Bots
{
    namespace
    {
        std::string Yards(float yards)
        {
            return std::to_string(int(std::lround(yards))) + "y";
        }

        bool ReadFloat(std::string const& word, float& value)
        {
            char* end = nullptr;
            value = std::strtof(word.c_str(), &end);
            return !word.empty() && *end == '\0' && std::isfinite(value);
        }

        bool ReadUint(std::string const& word, std::uint32_t& value)
        {
            char* end = nullptr;
            unsigned long const number = std::strtoul(word.c_str(), &end, 10);
            if (word.empty() || *end != '\0' || word[0] == '-')
                return false;
            value = std::uint32_t(number);
            return true;
        }

        // The note of a mark: no comment marker, words split by one space.
        std::string CleanNote(std::string const& note)
        {
            std::istringstream words(note);
            std::string word;
            std::string clean;
            while (words >> word)
            {
                word.erase(std::remove(word.begin(), word.end(), CommentMarker), word.end());
                if (word.empty())
                    continue;
                clean += (clean.empty() ? "" : " ") + word;
            }
            return clean;
        }
    }

    TraceResult ParseTrace(std::string const& text)
    {
        TraceResult result{false, {}, ""};
        Trace& trace = result.trace;
        bool haveHeader = false;
        bool gap = false;
        bool walk = false;
        for (DataLine const& line : TokenizeLines(text))
        {
            auto fail = [&](std::string const& message) {
                result.error = "line " + std::to_string(line.number) + ": " + message;
                return result;
            };
            std::vector<std::string> const& words = line.words;
            std::string const& key = words[0];
            if (key == "trace")
            {
                if (haveHeader)
                    return fail("a second trace line");
                if (words.size() != 3 || !ReadUint(words[2], trace.mapId))
                    return fail("use trace <name> <map>");
                trace.name = words[1];
                haveHeader = true;
                continue;
            }
            if (!haveHeader)
                return fail("the file must start with trace <name> <map>");
            if (key == "gap" || key == "walk")
            {
                if (words.size() != 1)
                    return fail(key + " takes no words");
                gap = true;
                walk = walk || key == "walk";
                continue;
            }
            if (key != "p" && key != "mark")
                return fail("unknown line " + key);
            TracePoint point;
            if (words.size() < 4 || (key == "p" && words.size() != 4) || !ReadFloat(words[1], point.position.x) ||
                !ReadFloat(words[2], point.position.y) || !ReadFloat(words[3], point.position.z))
                return fail("use " + key + " <x> <y> <z>" + (key == "mark" ? " [note]" : ""));
            point.mark = key == "mark";
            for (std::size_t i = 4; i < words.size(); ++i)
                point.note += (i > 4 ? " " : "") + words[i];
            point.joined = !gap && !trace.points.empty();
            point.walk = walk && !trace.points.empty();
            gap = walk = false;
            trace.points.push_back(point);
        }
        if (!haveHeader)
        {
            result.error = "the file has no trace line";
            return result;
        }
        result.ok = true;
        return result;
    }

    std::string WriteTrace(Trace const& trace)
    {
        std::ostringstream text;
        text << "# The GM trace of the route " << trace.name << ": .route trace, then .route build.\n";
        text << "trace " << trace.name << " " << trace.mapId << "\n";
        for (std::size_t i = 0; i < trace.points.size(); ++i)
        {
            TracePoint const& point = trace.points[i];
            if (i > 0 && point.walk)
                text << "walk\n";
            else if (i > 0 && !point.joined)
                text << "gap\n";
            char line[128];
            std::snprintf(line, sizeof(line), "%s %.2f %.2f %.2f", point.mark ? "mark" : "p", point.position.x,
                point.position.y, point.position.z);
            text << line;
            if (point.mark && !point.note.empty())
                text << " " << point.note;
            text << "\n";
        }
        return text.str();
    }

    float TraceLength(Trace const& trace)
    {
        float length = 0.0f;
        for (std::size_t i = 1; i < trace.points.size(); ++i)
            if (trace.points[i].joined)
                length += Distance3d(trace.points[i - 1].position, trace.points[i].position);
        return length;
    }

    std::size_t MarkCount(Trace const& trace)
    {
        return std::size_t(std::count_if(trace.points.begin(), trace.points.end(),
            [](TracePoint const& point) { return point.mark; }));
    }

    std::string TraceStatusLine(Trace const& trace, TraceState state)
    {
        char const* const names[] = {"off", "on", "paused"};
        return std::string("[route] trace ") + names[int(state)] + " " + trace.name +
            " points=" + std::to_string(trace.points.size()) + " marks=" + std::to_string(MarkCount(trace)) +
            " length=" + Yards(TraceLength(trace));
    }

    TraceRecorder::TraceRecorder(Trace trace, TraceSettings const& settings)
        : m_trace(std::move(trace)), m_settings(settings)
    {
        m_breakNext = true;
        m_walkNext = !m_trace.points.empty();
    }

    bool TraceRecorder::TakeChanged()
    {
        bool const changed = m_changed;
        m_changed = false;
        return changed;
    }

    void TraceRecorder::Add(Position const& position, bool mark, std::string const& note)
    {
        TracePoint point;
        point.position = position;
        point.joined = !m_breakNext && !m_trace.points.empty();
        point.walk = m_walkNext && !m_trace.points.empty();
        point.mark = mark;
        point.note = CleanNote(note);
        m_trace.points.push_back(point);
        m_breakNext = m_walkNext = false;
        m_changed = true;
    }

    TraceReply TraceRecorder::Look(Position const& position, TraceMotion motion)
    {
        TraceReply reply;
        bool const jumped = m_hasLook && Distance3d(m_look, position) > m_settings.jump;
        float const jump = m_hasLook ? Distance3d(m_look, position) : 0.0f;
        m_hasLook = true;
        m_look = position;
        if (m_state != TraceState::On)
            return reply;

        if (motion != TraceMotion::Ground)
        {
            bool& warned = motion == TraceMotion::Flying ? m_flyWarned : m_awayWarned;
            if (!warned)
                reply.text = motion == TraceMotion::Flying ? "you fly: the trace leaves out the flight"
                                                           : "you are off the map of the trace: it leaves this out";
            warned = true;
            m_breakNext = true;
            m_waitForEnd = false;
            return reply;
        }
        m_flyWarned = m_awayWarned = false;

        if (jumped)
        {
            reply.text = "a jump of " + Yards(jump) + " (a teleport?): the trace leaves it out";
            m_breakNext = true;
            m_waitForEnd = false;
            Add(position, false, "");
            return reply;
        }
        if (m_trace.points.empty() || m_breakNext)
        {
            Add(position, false, "");
            return reply;
        }
        Position const& last = m_trace.points.back().position;
        if (m_waitForEnd)
        {
            if (Distance3d(last, position) <= m_settings.step)
                m_waitForEnd = false;
            return reply;
        }
        if (Distance3d(last, position) >= m_settings.step || std::fabs(position.z - last.z) >= m_settings.floorChange)
            Add(position, false, "");
        return reply;
    }

    TraceReply TraceRecorder::Mark(Position const& position, std::string const& note)
    {
        if (m_state != TraceState::On)
            return {false, "the trace is paused: resume it first"};
        m_waitForEnd = false;
        Add(position, true, note);
        return {};
    }

    TraceReply TraceRecorder::Pause()
    {
        if (m_state != TraceState::On)
            return {false, "the trace is paused already"};
        m_state = TraceState::Paused;
        return {};
    }

    TraceReply TraceRecorder::Resume(Position const& position)
    {
        if (m_state != TraceState::Paused)
            return {false, "the trace is not paused"};
        m_state = TraceState::On;
        m_hasLook = true;
        m_look = position;
        m_waitForEnd = false;
        TraceReply reply;
        if (!m_trace.points.empty() && !m_breakNext)
        {
            float const gap = Distance3d(m_trace.points.back().position, position);
            if (gap > m_settings.step)
            {
                reply.text = "a gap of " + Yards(gap) + " from the pause: the trace does not join it";
                m_breakNext = true;
            }
        }
        Add(position, false, "");
        return reply;
    }

    TraceReply TraceRecorder::Undo(float yards)
    {
        if (m_trace.points.empty())
            return {false, "the trace has no points"};
        std::vector<TracePoint>& points = m_trace.points;
        float along = 0.0f;
        bool removedStart = false;
        bool removedWalk = false;
        while (!points.empty() && along <= yards)
        {
            TracePoint const removed = points.back();
            points.pop_back();
            removedStart = removedStart || !removed.joined;
            removedWalk = removedWalk || removed.walk;
            if (!points.empty())
                along += Distance3d(points.back().position, removed.position);
        }
        m_changed = true;
        if (removedStart || points.empty())
        {
            // The undo took the start of a walk, or of a part after a gap.
            m_breakNext = true;
            m_walkNext = m_walkNext || removedWalk;
            m_waitForEnd = false;
            return {};
        }
        m_waitForEnd = true;
        return {true, "walk back to the end of the trace: it adds no point until you are there"};
    }
}
