#include "ClassModules.h"
#include "FakeBot.h"
#include "GroupCoordinator.h"
#include "TrailFollow.h"

#include <gtest/gtest.h>

#include <sstream>

using Bots::BotContext;
using Bots::BotRole;
using Bots::CommandState;
using Bots::GroupCoordinator;
using Bots::Position;
using Bots::StrategyStack;
using Bots::Tests::FakeBot;

// jvbastel/WOWAI#192: out of combat, the bots of a player-tank group walk the
// human tank's trail to a spot behind it, by role.
namespace
{
    Bots::Guid const Self = 0x11;
    Bots::Guid const Other = 0x12;
    Bots::Guid const Tank = 0x05;

    // A bot of a player-tank group whose human tank walks the places
    // `walk`: each bot notes the tank's trail on each think (WatchTankTrail).
    struct Bot
    {
        FakeBot fake{Self};
        GroupCoordinator group;
        StrategyStack stack;

        explicit Bot(BotRole role)
        {
            stack.Push(Bots::MakeCommandStrategy());
            fake.SetHealth(Self, 600, 600);
            fake.SetHealth(Other, 600, 600);
            fake.SetHealth(Tank, 600, 600);
            fake.SetPosition(Self, {0.0f, -2.0f, 0.0f});
            fake.SetPosition(Other, {2.0f, -2.0f, 0.0f});
            fake.SetGroupMembers({Tank});
            group.SetTank(Tank, true);
            group.SetMemberRole(Self, role);
        }

        void Walk(std::vector<Position> const& walk)
        {
            for (Position const& at : walk)
            {
                fake.SetPosition(Tank, at);
                Bots::WatchTankTrail(fake, group);
            }
        }

        std::string Think()
        {
            BotContext const context{fake, fake, group, CommandState::Follow, Tank};
            return stack.Think(context);
        }

        // The place of the last move deed.
        Position MovedTo() const
        {
            std::vector<std::string> const& deeds = fake.Deeds();
            Position at{0, 0, 0};
            if (deeds.empty())
                return at;
            std::istringstream line(deeds.back());
            std::string word;
            while (line >> word && word != "to")
                ;
            line >> at.x >> at.y >> at.z;
            return at;
        }
    };

    // A straight walk north from the origin to (0, `toY`), a step each yard.
    std::vector<Position> North(float fromY, float toY)
    {
        std::vector<Position> walk;
        for (float y = fromY; y <= toY; y += 1.0f)
            walk.push_back({0.0f, y, 0.0f});
        return walk;
    }
}

TEST(TrailFollow, EachRoleWalksToASpotInItsBandBehindTheTank)
{
    struct Case
    {
        BotRole role;
        float nearYards;
        float farYards;
    };
    for (Case const& role : {Case{BotRole::Melee, 3.0f, 6.0f}, Case{BotRole::Healer, 10.0f, 15.0f},
             Case{BotRole::Ranged, 12.0f, 20.0f}})
    {
        Bot bot(role.role);
        bot.Walk(North(0.0f, 40.0f));

        EXPECT_EQ(bot.Think(), "follow the tank's trail");
        Position const spot = bot.MovedTo();
        float const behind = 40.0f - spot.y;
        EXPECT_GE(behind, role.nearYards) << int(role.role);
        EXPECT_LE(behind, role.farYards) << int(role.role);
        // Beside the trail, but near it.
        EXPECT_LE(std::fabs(spot.x), 3.0f) << int(role.role);
    }
}

TEST(TrailFollow, TwoBotsOfOneRoleGetDifferentSpots)
{
    Bot bot(BotRole::Ranged);
    bot.Walk(North(0.0f, 40.0f));
    std::vector<Position> const way = Bots::TankWay({0.0f, 40.0f, 0.0f}, bot.group.GetTrail());

    // The first and the second of two ranged members, the third and the
    // fourth bot of the group.
    Position const first = Bots::FindTrailSpot(bot.fake, way, {0.0f, 0.0f, 0.0f}, BotRole::Ranged, 0, 2, 2);
    Position const second = Bots::FindTrailSpot(bot.fake, way, {0.0f, 0.0f, 0.0f}, BotRole::Ranged, 1, 2, 3);

    EXPECT_GT(Bots::Distance2d(first, second), 2.0f);
    for (Position const& spot : {first, second})
    {
        EXPECT_GE(40.0f - spot.y, 12.0f);
        EXPECT_LE(40.0f - spot.y, 20.0f);
    }
}

TEST(TrailFollow, TwoBotsOfOneRoleInTheGroupWalkToDifferentSpots)
{
    // Each bot of the group stands near its own spot (12-20 yards back),
    // so its move is to the spot itself.
    std::vector<Position> spots;
    for (Bots::Guid const self : {Self, Other})
    {
        Bot bot(BotRole::Ranged);
        bot.fake.SetGroupMembers({Tank, self == Self ? Other : Self});
        bot.group.SetMemberRole(Other, BotRole::Ranged);
        bot.Walk(North(0.0f, 40.0f));
        FakeBot view{self};
        view.SetHealth(Tank, 600, 600);
        view.SetPosition(self, {0.0f, 23.0f, 0.0f});
        view.SetPosition(Tank, {0.0f, 40.0f, 0.0f});
        view.SetGroupMembers({Tank, self == Self ? Other : Self});
        BotContext const context{view, view, bot.group, CommandState::Follow, Tank};

        EXPECT_EQ(bot.stack.Think(context), "follow the tank's trail");
        std::istringstream line(view.Deeds().back());
        std::string word;
        while (line >> word && word != "to")
            ;
        Position spot{0, 0, 0};
        line >> spot.x >> spot.y >> spot.z;
        spots.push_back(spot);
    }

    EXPECT_GT(Bots::Distance2d(spots[0], spots[1]), 2.0f);
}

TEST(TrailFollow, TheBotWalksAroundTheCornerOfTheTrailNotAcrossIt)
{
    Bot bot(BotRole::Healer);
    // North 20 yards, then east 20 yards.
    std::vector<Position> walk = North(0.0f, 20.0f);
    for (float x = 1.0f; x <= 20.0f; x += 1.0f)
        walk.push_back({x, 20.0f, 0.0f});
    bot.Walk(walk);

    EXPECT_EQ(bot.Think(), "follow the tank's trail");
    // The healer's spot is 10-15 yards back along the trail, on the east leg:
    // the bot first walks up the north leg to the corner.
    Position const step = bot.MovedTo();
    EXPECT_LE(Bots::Distance2d(step, {0.0f, 20.0f, 0.0f}), 2.5f);
}

TEST(TrailFollow, WhenTheTankWalksBackTheBotsWalkBackAndStayBehindIt)
{
    Bot bot(BotRole::Healer);
    bot.Walk(North(0.0f, 40.0f));
    // The healer stood at its spot behind the tank.
    bot.fake.SetPosition(Self, {1.5f, 27.5f, 0.0f});
    // The tank shot a pack and walks back south, past the healer.
    std::vector<Position> back;
    for (float y = 39.0f; y >= 22.0f; y -= 1.0f)
        back.push_back({0.0f, y, 0.0f});
    bot.Walk(back);

    // The trail ends where the tank stands again.
    EXPECT_LE(bot.group.GetTrail().back().y, 24.0f);
    EXPECT_EQ(bot.Think(), "follow the tank's trail");
    Position const step = bot.MovedTo();
    // Back south, behind the tank.
    EXPECT_LT(step.y, 22.0f);
    EXPECT_GE(22.0f - step.y, 0.0f);
    // At its spot: 10-15 yards behind the tank.
    bot.fake.SetPosition(Self, step);
    bot.fake.ClearDeeds();
    bot.Think();
    Position const spot = bot.fake.Deeds().empty() ? step : bot.MovedTo();
    EXPECT_GE(22.0f - spot.y, 10.0f);
    EXPECT_LE(22.0f - spot.y, 15.0f);
}

TEST(TrailFollow, InCombatTheTrailFollowDoesNotFire)
{
    Bot bot(BotRole::Ranged);
    bot.Walk(North(0.0f, 40.0f));
    bot.fake.SetInCombat(Self);

    EXPECT_NE(bot.Think(), "follow the tank's trail");
}

TEST(TrailFollow, ABotInPlaceStandsStill)
{
    Bot bot(BotRole::Melee);
    bot.Walk(North(0.0f, 40.0f));
    ASSERT_EQ(bot.Think(), "follow the tank's trail");
    bot.fake.SetPosition(Self, bot.MovedTo());
    bot.fake.ClearDeeds();

    EXPECT_EQ(bot.Think(), "");
    EXPECT_TRUE(bot.fake.Deeds().empty());
}

TEST(TrailFollow, ABotTankGroupFollowsAsBefore)
{
    Bot bot(BotRole::Healer);
    bot.group.SetTank(Tank);
    bot.Walk(North(0.0f, 40.0f));

    // No trail of the bots: the bot tank keeps its own (WatchTrail).
    EXPECT_TRUE(bot.group.GetTrail().empty());
    EXPECT_EQ(bot.Think(), "follow the player");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"follow 0x0000000000000005"}));
}

TEST(TrailFollow, ABotTanksTrailIsNotCutBackWhenItWalksBack)
{
    GroupCoordinator group;
    group.SetTank(Tank);
    for (float y : {0.0f, 2.0f, 4.0f, 6.0f, 4.0f, 2.0f})
        group.KeepTrail({0.0f, y, 0.0f}, false);

    EXPECT_EQ(group.GetTrail().size(), 6u);
}

TEST(TrailFollow, ABotInPlaceThatStillFollowsTheTankStopsThere)
{
    Bot bot(BotRole::Melee);
    bot.Walk(North(0.0f, 40.0f));
    ASSERT_EQ(bot.Think(), "follow the tank's trail");
    bot.fake.SetPosition(Self, bot.MovedTo());
    // A `follow` command started the follow of the game.
    bot.fake.SetFollowTarget(Tank);
    bot.fake.ClearDeeds();

    EXPECT_EQ(bot.Think(), "follow the tank's trail");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"stand"}));
}

// jvbastel/WOWAI#192, merge review: every bot of the group writes the one
// trail of the coordinator.
TEST(TrailFollow, ABotThatDoesNotSeeTheTankLeavesTheTrail)
{
    Bot bot(BotRole::Melee);
    bot.Walk(North(0.0f, 20.0f));
    std::size_t const points = bot.group.GetTrail().size();
    ASSERT_GT(points, 1u);

    // A second bot of the group, on whose view the tank is not found.
    FakeBot blind{Other};
    Bots::WatchTankTrail(blind, bot.group);

    EXPECT_EQ(bot.group.GetTrail().size(), points);
}

TEST(TrailFollow, TwoBotsThatNoteOneTankPlaceAddItOnce)
{
    Bot bot(BotRole::Melee);
    bot.Walk(North(0.0f, 20.0f));
    std::size_t const points = bot.group.GetTrail().size();

    FakeBot other{Other};
    other.SetHealth(Tank, 600, 600);
    other.SetPosition(Tank, {0.0f, 20.0f, 0.0f});
    Bots::WatchTankTrail(other, bot.group);
    Bots::WatchTankTrail(bot.fake, bot.group);

    EXPECT_EQ(bot.group.GetTrail().size(), points);
}

TEST(TrailFollow, AWalkBackCutsTheTrailToTheNewestNearPoint)
{
    GroupCoordinator group;
    group.SetTank(Tank, true);
    for (float y : {0.0f, 2.0f, 4.0f, 6.0f})
        group.KeepTrail({0.0f, y, 0.0f}, false);

    // Near both the first and the second point: only the points after the
    // second go.
    group.KeepTrail({0.0f, 0.9f, 0.0f}, false);

    EXPECT_EQ(group.GetTrail().size(), 2u);
}

// Spec #188 fix-up (S3, story 38): the trail follow keys on the bot's own
// combat. A retreat pull: the tank shoots a pack (it is in combat) and walks
// back. A bot not in the fight yet walks back with it along the trail.
TEST(TrailFollow, ABotNotInTheFightWalksBackWithTheTankOnARetreatPull)
{
    Bot bot(BotRole::Healer);
    bot.Walk(North(0.0f, 40.0f));
    bot.fake.SetPosition(Self, {1.5f, 27.5f, 0.0f});
    // The tank shot a pack: it is in combat, and walks back south.
    bot.fake.SetInCombat(Tank);
    std::vector<Position> back;
    for (float y = 39.0f; y >= 22.0f; y -= 1.0f)
        back.push_back({0.0f, y, 0.0f});
    bot.Walk(back);

    EXPECT_EQ(bot.Think(), "follow the tank's trail");
    Position const step = bot.MovedTo();
    // Back south, behind the tank.
    EXPECT_LT(step.y, 22.0f);
}
