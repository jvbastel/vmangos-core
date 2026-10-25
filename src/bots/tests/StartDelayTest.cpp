#include "StartDelay.h"

#include <gtest/gtest.h>

#include <vector>

using Bots::DrawStartDelay;
using Bots::RandomSource;
using Bots::StartDelaySettings;

namespace
{
    // A random source that gives back fixed numbers and keeps the limits of
    // each draw.
    class FixedRandom : public RandomSource
    {
    public:
        explicit FixedRandom(std::vector<std::uint32_t> values) : m_values(values) {}

        std::uint32_t Between(std::uint32_t low, std::uint32_t high) override
        {
            lows.push_back(low);
            highs.push_back(high);
            std::uint32_t const value = m_values[m_next % m_values.size()];
            ++m_next;
            return value;
        }

        std::vector<std::uint32_t> lows;
        std::vector<std::uint32_t> highs;

    private:
        std::vector<std::uint32_t> m_values;
        std::size_t m_next = 0;
    };
}

TEST(StartDelay, TheDefaultLimitsAre300To1500Ms)
{
    StartDelaySettings const settings;

    EXPECT_EQ(settings.minMs, 300u);
    EXPECT_EQ(settings.maxMs, 1500u);
}

TEST(StartDelay, TheDrawAsksTheRandomSourceForANumberInTheLimits)
{
    FixedRandom random({700});

    EXPECT_EQ(DrawStartDelay({200, 900}, random), 700u);
    EXPECT_EQ(random.lows, (std::vector<std::uint32_t>{200}));
    EXPECT_EQ(random.highs, (std::vector<std::uint32_t>{900}));
}

TEST(StartDelay, ANumberOutsideTheLimitsIsClampedToThem)
{
    FixedRandom random({50, 5000});

    EXPECT_EQ(DrawStartDelay({300, 1500}, random), 300u);
    EXPECT_EQ(DrawStartDelay({300, 1500}, random), 1500u);
}

TEST(StartDelay, AMaximumBelowTheMinimumGivesTheMinimum)
{
    FixedRandom random({100});

    EXPECT_EQ(DrawStartDelay({800, 400}, random), 800u);
    EXPECT_EQ(random.highs, (std::vector<std::uint32_t>{800}));
}

TEST(StartDelay, EqualLimitsGiveThatDelay)
{
    FixedRandom random({0});

    EXPECT_EQ(DrawStartDelay({0, 0}, random), 0u);
}

TEST(StartDelay, TheSeededSourceStaysInTheLimitsAndRepeatsForASeed)
{
    Bots::SeededRandom first(42);
    Bots::SeededRandom second(42);

    for (int i = 0; i < 1000; ++i)
    {
        std::uint32_t const delay = DrawStartDelay({300, 1500}, first);
        EXPECT_GE(delay, 300u);
        EXPECT_LE(delay, 1500u);
        EXPECT_EQ(DrawStartDelay({300, 1500}, second), delay);
    }
}

TEST(StartDelay, TheSeededSourceGivesNewDelays)
{
    Bots::SeededRandom random(7);
    std::uint32_t const first = DrawStartDelay({300, 1500}, random);
    bool changed = false;
    for (int i = 0; i < 20; ++i)
        changed = changed || DrawStartDelay({300, 1500}, random) != first;

    EXPECT_TRUE(changed);
}

using Bots::CommandState;
using Bots::StartFacts;
using Bots::StartGate;
using Bots::StartReason;

namespace
{
    StartDelaySettings const Limits{300, 1500};

    // A bot that follows the player and stands next to it.
    StartFacts Standing()
    {
        StartFacts facts;
        facts.command = CommandState::Follow;
        facts.inCombat = false;
        facts.followsPlayer = true;
        facts.botStands = true;
        facts.playerMoves = false;
        return facts;
    }

    StartFacts PlayerMoves()
    {
        StartFacts facts = Standing();
        facts.playerMoves = true;
        return facts;
    }
}

TEST(StartGate, AMoveCommandWaitsANewDelayBeforeTheBotStarts)
{
    FixedRandom random({400, 900});
    StartGate gate;

    EXPECT_EQ(gate.OnMoveCommand(Limits, random), 400u);
    EXPECT_TRUE(gate.Waiting());
    EXPECT_EQ(gate.Update(399, Standing(), Limits, random).step, StartGate::Step::None);
    StartGate::Result const done = gate.Update(1, Standing(), Limits, random);
    EXPECT_EQ(done.step, StartGate::Step::Start);
    EXPECT_EQ(done.reason, StartReason::Command);
    EXPECT_FALSE(gate.Waiting());

    EXPECT_EQ(gate.OnMoveCommand(Limits, random), 900u);
}

TEST(StartGate, ANoWaitCommandEndsTheWaitWithNoStart)
{
    FixedRandom random({400});
    StartGate gate;
    gate.OnMoveCommand(Limits, random);

    gate.Cancel();

    EXPECT_FALSE(gate.Waiting());
    EXPECT_EQ(gate.Update(1000, Standing(), Limits, random).step, StartGate::Step::None);
}

TEST(StartGate, WhenThePlayerStartsToMoveTheStandingBotWaits)
{
    FixedRandom random({600});
    StartGate gate;
    gate.Update(100, Standing(), Limits, random);

    StartGate::Result const wait = gate.Update(100, PlayerMoves(), Limits, random);

    EXPECT_EQ(wait.step, StartGate::Step::Wait);
    EXPECT_EQ(wait.reason, StartReason::PlayerMoves);
    EXPECT_EQ(wait.delayMs, 600u);
    EXPECT_EQ(gate.Update(599, PlayerMoves(), Limits, random).step, StartGate::Step::None);
    StartGate::Result const start = gate.Update(1, PlayerMoves(), Limits, random);
    EXPECT_EQ(start.step, StartGate::Step::Start);
    EXPECT_EQ(start.reason, StartReason::PlayerMoves);
}

TEST(StartGate, APlayerWhoKeepsMovingStartsNoNewWait)
{
    FixedRandom random({300});
    StartGate gate;
    gate.Update(100, Standing(), Limits, random);
    gate.Update(100, PlayerMoves(), Limits, random);
    gate.Update(300, PlayerMoves(), Limits, random);

    EXPECT_EQ(gate.Update(100, PlayerMoves(), Limits, random).step, StartGate::Step::None);
    EXPECT_EQ(gate.Update(100, PlayerMoves(), Limits, random).step, StartGate::Step::None);
}

TEST(StartGate, EachStartOfThePlayerGivesANewDelay)
{
    FixedRandom random({300, 1200});
    StartGate gate;
    gate.Update(100, Standing(), Limits, random);
    EXPECT_EQ(gate.Update(100, PlayerMoves(), Limits, random).delayMs, 300u);
    gate.Update(300, PlayerMoves(), Limits, random);
    gate.Update(100, Standing(), Limits, random);

    EXPECT_EQ(gate.Update(100, PlayerMoves(), Limits, random).delayMs, 1200u);
}

TEST(StartGate, ABotThatStillWalksDoesNotWaitWhenThePlayerStarts)
{
    FixedRandom random({300});
    StartGate gate;
    StartFacts walks = Standing();
    walks.botStands = false;
    gate.Update(100, walks, Limits, random);
    walks.playerMoves = true;

    EXPECT_EQ(gate.Update(100, walks, Limits, random).step, StartGate::Step::None);
}

TEST(StartGate, OnlyAFollowingBotWaitsForThePlayer)
{
    FixedRandom random({300});
    for (CommandState command : {CommandState::None, CommandState::Stay, CommandState::Come})
    {
        StartGate gate;
        StartFacts facts = Standing();
        facts.command = command;
        gate.Update(100, facts, Limits, random);
        facts.playerMoves = true;

        EXPECT_EQ(gate.Update(100, facts, Limits, random).step, StartGate::Step::None);
    }
}

TEST(StartGate, ABotThatFollowsSomeoneElseDoesNotWaitForThePlayer)
{
    FixedRandom random({300});
    StartGate gate;
    StartFacts facts = Standing();
    facts.followsPlayer = false;
    gate.Update(100, facts, Limits, random);
    facts.playerMoves = true;

    EXPECT_EQ(gate.Update(100, facts, Limits, random).step, StartGate::Step::None);
}

TEST(StartGate, AFollowingBotWaitsWhenTheFightEnds)
{
    FixedRandom random({800});
    StartGate gate;
    StartFacts fight = Standing();
    fight.inCombat = true;
    fight.followsPlayer = false;
    gate.Update(100, fight, Limits, random);

    StartFacts after = fight;
    after.inCombat = false;
    StartGate::Result const wait = gate.Update(100, after, Limits, random);

    EXPECT_EQ(wait.step, StartGate::Step::Wait);
    EXPECT_EQ(wait.reason, StartReason::FightEnds);
    EXPECT_EQ(wait.delayMs, 800u);
    EXPECT_TRUE(gate.Waiting());
    EXPECT_EQ(gate.Update(800, after, Limits, random).step, StartGate::Step::Start);
}

TEST(StartGate, AStayingBotDoesNotWaitWhenTheFightEnds)
{
    FixedRandom random({800});
    StartGate gate;
    StartFacts fight = Standing();
    fight.command = CommandState::Stay;
    fight.inCombat = true;
    gate.Update(100, fight, Limits, random);
    fight.inCombat = false;

    EXPECT_EQ(gate.Update(100, fight, Limits, random).step, StartGate::Step::None);
}

TEST(StartGate, AMoveCommandDuringAWaitDrawsANewDelay)
{
    FixedRandom random({1000, 300});
    StartGate gate;
    gate.Update(100, Standing(), Limits, random);
    gate.Update(100, PlayerMoves(), Limits, random);

    EXPECT_EQ(gate.OnMoveCommand(Limits, random), 300u);
    StartGate::Result const start = gate.Update(300, PlayerMoves(), Limits, random);
    EXPECT_EQ(start.step, StartGate::Step::Start);
    EXPECT_EQ(start.reason, StartReason::Command);
}
