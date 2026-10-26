#ifndef WOWAI_BOTS_ROUTE_TRACE_H
#define WOWAI_BOTS_ROUTE_TRACE_H

#include "BotTypes.h"
#include "RouteGenerator.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The GM trace of a dungeon route: the path that a GM walks one time, as the
// group walks it. The GM command `.route trace` records it, and `.route
// build` makes the route file around it (BuildTraceRoute), with the pack and
// quest rules of the route generator (RouteGenerator.h). Only the source of
// the center line differs: the trace, not a navmesh path.
//
// The trace file sits next to the route file, for example
// `routes/ragefire_chasm.trace.txt`:
//
//   trace ragefire_chasm 389          # route name and map id
//   p 3.81 -14.82 -17.84              # a trace point
//   mark 5.10 -12.00 -17.80 corner    # a checkpoint, with an optional note
//   gap                               # the next point does not join the one before
//   walk                              # the next point starts a later walk
//
// - `trace <name> <map>` comes first, once.
// - `p` and `mark` are the points in walk order. A checkpoint becomes a route
//   point at its exact position.
// - `gap` comes before the point after a pause, a teleport, or a flight: the
//   GM did not walk from the point before to it.
// - `walk` comes before the first point of a later `.route trace start`, for
//   example an extra walk to a quest pack.
namespace Bots
{
    struct TracePoint
    {
        Position position;
        // The GM walked from the point before to this one.
        bool joined = true;
        // The first point of a later walk. It is never joined.
        bool walk = false;
        bool mark = false;
        std::string note; // Of a mark. Words split by one space.
    };

    struct Trace
    {
        std::string name;
        std::uint32_t mapId = 0;
        std::vector<TracePoint> points;
    };

    struct TraceResult
    {
        bool ok;
        Trace trace;       // Only when ok.
        std::string error; // "line <n>: ..." when not ok.
    };

    TraceResult ParseTrace(std::string const& text);
    // The text that ParseTrace reads back to the same trace.
    std::string WriteTrace(Trace const& trace);

    // The length in yards along the joined points.
    float TraceLength(Trace const& trace);
    std::size_t MarkCount(Trace const& trace);

    // The settings of the trace recorder, in yards.
    struct TraceSettings
    {
        // A new point when the GM is this far from the last point.
        float step = 4.0f;
        // A new point when the height changed this much: another floor.
        float floorChange = 3.0f;
        // A move this long between two looks is a teleport.
        float jump = 20.0f;
        // `.route trace undo` with no length.
        float undo = 10.0f;
    };

    enum class TraceState
    {
        Off,
        On,
        Paused,
    };

    // How the GM moves at a look of the recorder.
    enum class TraceMotion
    {
        Ground,
        Flying,
        // Not on the map of the trace, or in a teleport.
        Away,
    };

    // The answer of a recorder call. `ok` false: `text` is the error, and
    // nothing changed. `ok` true: `text` is a warning for the GM, or empty.
    struct TraceReply
    {
        bool ok = true;
        std::string text;
    };

    // The recorder of `.route trace`. The game calls Look with the GM's
    // position a few times a second, and the commands call the others.
    class TraceRecorder
    {
    public:
        // Starts a trace. An existing trace with points gets a later walk at
        // its end.
        TraceRecorder(Trace trace, TraceSettings const& settings);

        // The GM is at `position`. A warning, for a teleport or a flight,
        // comes once per jump or flight.
        TraceReply Look(Position const& position, TraceMotion motion);
        // A checkpoint at `position`.
        TraceReply Mark(Position const& position, std::string const& note);
        TraceReply Pause();
        // The next point is `position`, which does not join the pause
        // position when it is more than a step away.
        TraceReply Resume(Position const& position);
        // Removes the points within `yards` of the end, along the trace. The
        // trace then waits until the GM walks back to its new end.
        TraceReply Undo(float yards);

        Trace const& GetTrace() const { return m_trace; }
        TraceState State() const { return m_state; }
        // True when points were added or removed since the last call.
        bool TakeChanged();

    private:
        void Add(Position const& position, bool mark, std::string const& note);

        Trace m_trace;
        TraceSettings m_settings;
        TraceState m_state = TraceState::On;
        bool m_hasLook = false;
        Position m_look{};
        // The next point does not join the last one.
        bool m_breakNext = false;
        // The next point starts a later walk.
        bool m_walkNext = false;
        bool m_flyWarned = false;
        bool m_awayWarned = false;
        // After an undo: no point until the GM is back at the end.
        bool m_waitForEnd = false;
        bool m_changed = false;
    };

    // The one line that each trace command answers with, for the GM and the
    // RouteRecorder addon:
    //   [route] trace on ragefire_chasm points=212 marks=9 length=640y
    std::string TraceStatusLine(Trace const& trace, TraceState state);

    // The input of `.route build`: the trace as the center line, and the
    // data of the route generator. Of the settings, `.route build` uses
    // aggroRange, margin, clusterDistance, pointSpacing, and pointRadius.
    struct TraceBuildInput
    {
        Trace trace;
        std::vector<GeneratorSpawn> spawns;
        std::vector<std::uint32_t> bosses;
        std::vector<GeneratorQuest> quests;
        GeneratorSettings settings;
        // A visit spot must be this near the trace.
        float visitRange = 5.0f;
        std::function<std::string(std::uint32_t entry)> entryName;
    };

    // A pack off the trace that holds spawns of an objective.
    struct NearPack
    {
        std::vector<RouteMob> mobs;
        Position position; // The spawn of its first mob.
        std::uint32_t count; // Its spawns of the objective.
        float distance;      // From the trace.
    };

    // A quest objective that the trace does not meet.
    struct UnmetObjective
    {
        std::uint32_t quest;
        QuestObjective objective;
        std::uint32_t have;
        std::uint32_t need;
        // For a kill or an item: the nearest packs that would meet it,
        // nearest first. For a visit: empty, and `distance` is from the trace.
        std::vector<NearPack> nearest;
        float distance = 0.0f;
    };

    struct TraceBuild
    {
        bool ok = false;
        std::string error; // When not ok.
        Route route;
        std::vector<ObjectiveCheck> objectives;
        std::vector<UnmetObjective> unmet;
        // Bosses that the trace does not pass within aggro range.
        std::vector<std::uint32_t> unmetBosses;
        std::vector<std::string> warnings;
    };

    // The route around the trace:
    // - The center line: the walks of the trace in order. A later walk that
    //   starts near the line is a branch: the route walks it out and back
    //   there. Any other later walk follows the end.
    // - The packs, the "on the route" rule (FirstApproach), and the pack
    //   names of the route generator, on the center line.
    // - A pull point on the line where it first comes within aggro range
    //   plus the margin of each pack, outside its aggro range.
    // - Route points along the line every pointSpacing, with no sideways
    //   offset and no alternatives. Each checkpoint is a route point at its
    //   exact position, and the spacing counts again from it, and from the
    //   point after a gap.
    // - The quests of the generator: a visit point on the line nearest the
    //   spot. No pack off the line is added: an unmet kill count or item
    //   lists the nearest packs that hold it.
    TraceBuild BuildTraceRoute(TraceBuildInput const& input);
}

#endif
