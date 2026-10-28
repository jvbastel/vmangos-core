#include "RunTrace.h"

#include <gtest/gtest.h>

using Bots::MemberState;
using Bots::RunTrace;

namespace
{
    MemberState Grukk(float x, std::string const& target = "", std::uint32_t spell = 0, std::string const& spellName = "")
    {
        MemberState state;
        state.name = "Grukk";
        state.position = {x, 2.0f, 3.0f};
        state.target = target;
        state.spell = spell;
        state.spellName = spellName;
        return state;
    }
}

TEST(RunTrace, WritesOneLinePerEventWithItsRunTime)
{
    RunTrace trace;
    trace.Add(1500, "Ezzra says in party: pull");
    trace.Add(62250, "line 12: fight pack1");

    EXPECT_EQ(trace.Lines(), (std::vector<std::string>{
        "1.5 Ezzra says in party: pull",
        "62.2 line 12: fight pack1"}));
}

TEST(RunTrace, KeepsOnlyTheLastThreeMinutes)
{
    RunTrace trace;
    trace.Add(0, "old");
    trace.Add(10000, "kept");
    trace.Add(190000, "new");

    EXPECT_EQ(trace.Lines(), (std::vector<std::string>{"10.0 kept", "190.0 new"}));
}

TEST(RunTrace, KeepsTheWholeRunWhenAsked)
{
    RunTrace trace(Bots::TraceKeepWholeRun);
    trace.Add(0, "old");
    trace.Add(10000, "kept");
    trace.Add(3600000, "new");

    EXPECT_EQ(trace.Lines(), (std::vector<std::string>{"0.0 old", "10.0 kept", "3600.0 new"}));
}

TEST(RunTrace, AMemberThatStandsStillWritesItsPlaceOnce)
{
    RunTrace trace;
    trace.Observe(0, Grukk(1.0f));
    trace.Observe(5000, Grukk(1.0f));
    trace.Observe(9000, Grukk(1.5f));

    EXPECT_EQ(trace.Lines(), (std::vector<std::string>{"0.0 Grukk at 1.00 2.00 3.00"}));
}

TEST(RunTrace, AMovingMemberWritesItsPlaceAtMostOnceASecond)
{
    RunTrace trace;
    trace.Observe(0, Grukk(0.0f));
    trace.Observe(500, Grukk(4.0f));
    trace.Observe(1000, Grukk(7.0f));

    EXPECT_EQ(trace.Lines(), (std::vector<std::string>{
        "0.0 Grukk at 0.00 2.00 3.00",
        "1.0 Grukk at 7.00 2.00 3.00"}));
}

TEST(RunTrace, WritesEachNewTargetAndEachNewCast)
{
    RunTrace trace;
    trace.Observe(0, Grukk(0.0f));
    trace.Observe(100, Grukk(0.0f, "Earthborer 38043"));
    trace.Observe(200, Grukk(0.0f, "Earthborer 38043", 2457, "Battle Stance"));
    trace.Observe(300, Grukk(0.0f, "Earthborer 38043", 2457, "Battle Stance"));
    trace.Observe(400, Grukk(0.0f, "", 0));

    EXPECT_EQ(trace.Lines(), (std::vector<std::string>{
        "0.0 Grukk at 0.00 2.00 3.00",
        "0.1 Grukk targets Earthborer 38043",
        "0.2 Grukk casts 2457 Battle Stance",
        "0.4 Grukk targets nothing"}));
}

TEST(RunTrace, TheTextHasOneEventPerLine)
{
    RunTrace trace;
    trace.Add(0, "a");
    trace.Add(1000, "b");

    EXPECT_EQ(trace.Text(), "0.0 a\n1.0 b\n");
}

TEST(RunTrace, GivesEachNewLineOnceForTheLiveFile)
{
    RunTrace trace(1000);
    trace.Add(500, "line 12: fight pack1");
    trace.Observe(600, Grukk(1.0f));

    EXPECT_EQ(trace.TakeNew(), (std::vector<std::string>{"0.5 line 12: fight pack1", "0.6 Grukk at 1.00 2.00 3.00"}));
    EXPECT_TRUE(trace.TakeNew().empty());

    // A new line comes even when the last 3 minutes no longer hold the old ones.
    trace.Add(5000, "wipe 1");
    EXPECT_EQ(trace.TakeNew(), (std::vector<std::string>{"5.0 wipe 1"}));
}

// jvbastel/WOWAI#121 (for #109): an enemy of the fight that starts to flee,
// with where it was then.
TEST(RunTrace, AFleeLineNamesTheEnemyAndItsPlace)
{
    EXPECT_EQ(Bots::FleeLine("Ragefire Trogg 48661", {-240.712f, -8.03f, -61.5f}),
        "Ragefire Trogg 48661 flees at -240.71 -8.03 -61.50");
}
