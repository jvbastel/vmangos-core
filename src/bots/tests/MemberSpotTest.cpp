#include "BotGeometry.h"
#include "FakeBot.h"
#include "ClassModules.h"
#include "DeathAndRest.h"
#include "MemberSpot.h"

#include <gtest/gtest.h>

using Bots::BotRole;
using Bots::Distance3d;
using Bots::Position;
using Bots::Tests::FakeBot;

// The member spots of a pull (jvbastel/WOWAI#106): before the tank pulls,
// each other bot goes to a spot of its own near the fight spot.
namespace
{
    Bots::Guid const Self = 0x12;
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Kobold = 0xF130000302000022;
    Bots::Guid const Wolf = 0xF130000302000033;

    // The tank's fight spot at (0, 0, 0), on its trail along the x axis from
    // x = -30, and the kobold 25 yards ahead. The bot stands at the start of
    // the trail.
    struct Spots
    {
        FakeBot fake{Self};
        std::vector<Position> trail;
        Position const fightSpot{0, 0, 0};

        Spots()
        {
            fake.SetPosition(Self, {-30, 0, 0});
            fake.SetPosition(Tank, {0, 0, 0});
            for (float x = -30.0f; x <= 0.0f; x += 2.0f)
                trail.push_back({x, 0.0f, 0.0f});
            AddEnemy(Kobold, {25, 0, 0}, 10.0f);
        }

        void AddEnemy(Bots::Guid enemy, Position at, float aggroYards)
        {
            fake.SetPosition(enemy, at);
            fake.AddEnemy(enemy, at, aggroYards);
        }

        bool Find(BotRole role, Position& spot, std::vector<Position> const& taken = {}) const
        {
            return Bots::FindMemberSpot(fake, role, fightSpot, trail, Kobold, taken, spot);
        }
    };

    ::testing::AssertionResult BackFromTheFightSpot(Position const& spot)
    {
        float const yards = Distance3d(spot, {0, 0, 0});
        if (yards >= Bots::MemberBackMinYards && yards <= Bots::MemberBackMaxYards && spot.x < 0)
            return ::testing::AssertionSuccess();
        return ::testing::AssertionFailure() << "spot " << spot.x << ' ' << spot.y << " is " << yards
                                             << " yards from the fight spot";
    }
}

TEST(MemberSpot, RangedStandsFifteenToTwentyFiveYardsBackOnTheClearedWay)
{
    Spots spots;
    Position spot{};

    ASSERT_TRUE(spots.Find(BotRole::Ranged, spot));

    // The first trail point 15 yards or more back.
    EXPECT_EQ(spot.x, -16.0f);
    EXPECT_EQ(spot.y, 0.0f);
}

TEST(MemberSpot, RangedAndHealerSpreadFiveYardsFromTheOtherMembers)
{
    Spots spots;
    std::vector<Position> taken;
    for (BotRole role : {BotRole::Ranged, BotRole::Healer, BotRole::Ranged, BotRole::Ranged})
    {
        Position spot{};
        ASSERT_TRUE(spots.Find(role, spot, taken));
        EXPECT_TRUE(BackFromTheFightSpot(spot));
        for (Position const& other : taken)
            EXPECT_GE(Distance3d(spot, other), Bots::MemberSpreadYards);
        taken.push_back(spot);
    }
}

TEST(MemberSpot, WithoutATrailTheSpotIsBackAwayFromTheTarget)
{
    Spots spots;
    spots.trail.clear();
    Position spot{};

    ASSERT_TRUE(spots.Find(BotRole::Ranged, spot));

    EXPECT_EQ(spot.x, -20.0f);
    EXPECT_NEAR(spot.y, 0.0f, 0.01f);
}

// The first spot of the fan, 20 yards back, is on ground 10 yards lower
// (a ledge, as in the two_packs_apart scenario): the bot would need a long
// way round to get there.
TEST(MemberSpot, NoSpotOnGroundFarAboveOrBelowTheFightSpot)
{
    Spots spots;
    spots.trail.clear();
    spots.fake.AddGroundFromX(-21.0f, -10.0f);
    Position spot{};

    ASSERT_TRUE(spots.Find(BotRole::Ranged, spot));

    EXPECT_EQ(spot.x, -25.0f);
    EXPECT_EQ(spot.z, 0.0f);
}

// The review of jvbastel/WOWAI#100: the healer heals the tank at the fight
// spot, so its spot sees the fight spot, not where the tank stands now.
TEST(MemberSpot, TheHealersSpotSeesTheFightSpot)
{
    Spots spots;
    // The tank stands back on the trail now; a wall at x = -14.5 hides the
    // fight spot from the spots of the trail.
    spots.fake.SetPosition(Tank, {-25, 0, 0});
    spots.fake.AddSightWallAtX(-14.5f);
    Position healer{};

    ASSERT_TRUE(spots.Find(BotRole::Healer, healer));

    EXPECT_GT(healer.x, -14.5f);
    EXPECT_TRUE(BackFromTheFightSpot(healer));
}

TEST(MemberSpot, TheSpotKeepsOutOfTheAggroOfAnotherPackWithTheMargin)
{
    Spots spots;
    // Aggro 10 and the margin 5: x = -16 (14.1 yards) and -18 (14.6) pull it.
    spots.AddEnemy(Wolf, {-14, 14, 0}, 10.0f);
    Position spot{};

    ASSERT_TRUE(spots.Find(BotRole::Ranged, spot));

    EXPECT_EQ(spot.x, -20.0f);
}

TEST(MemberSpot, MeleeStandsAtTheFightSpotEvenInTheAggroOfThePulledPack)
{
    Spots spots;
    spots.AddEnemy(Kobold, {25, 0, 0}, 30.0f);
    Position spot{};

    ASSERT_TRUE(spots.Find(BotRole::Melee, spot));

    EXPECT_EQ(spot.x, -Bots::MeleeBackYards);
    EXPECT_EQ(spot.y, 0.0f);
}

TEST(MemberSpot, NoSpotWhenNoneIsSafe)
{
    Spots spots;
    spots.fake.AddLavaFromX(-1000.0f);
    Position spot{};

    EXPECT_FALSE(spots.Find(BotRole::Ranged, spot));
    EXPECT_FALSE(spots.Find(BotRole::Melee, spot));
}

// The rule "go to the member spot": on the pull, once the tank has its fight
// spot, the bot walks to its spot and holds there until the pull holds.
namespace
{
    Bots::Guid const Player = 0x05;
    Bots::Guid const Other = 0x13;

    struct MemberBot
    {
        Spots spots;
        Bots::GroupCoordinator group;
        Bots::StrategyStack stack;

        explicit MemberBot(BotRole role = BotRole::Ranged)
        {
            stack.Push(Bots::MakeCommandStrategy());
            stack.Push(Bots::MakeMemberSpotStrategy(role));
            FakeBot& fake = spots.fake;
            fake.SetHealth(Self, 600, 600);
            fake.SetHealth(Tank, 600, 600);
            fake.SetHealth(Player, 600, 600);
            fake.SetPosition(Player, {-31, 0, 0});
            fake.SetGroupMembers({Tank, Player});
            for (Position const& point : spots.trail)
                group.KeepTrail(point, false);
            group.SetTank(Tank);
            group.StartPull(Tank, Kobold);
            group.SetPullSpot(Tank, {0, 0, 0});
        }

        std::string Think(Bots::CommandState command = Bots::CommandState::Follow)
        {
            Bots::BotContext const context{spots.fake, spots.fake, group, command, Player};
            return stack.Think(context);
        }

        Bots::MemberSpot Mine() const
        {
            for (Bots::MemberSpot const& spot : group.GetMemberSpots())
                if (spot.bot == Self)
                    return spot;
            return {};
        }
    };
}

TEST(MemberSpotRule, OnThePullTheBotWalksToItsSpot)
{
    MemberBot bot;

    EXPECT_EQ(bot.Think(), "go to the member spot");

    EXPECT_EQ(bot.spots.fake.Deeds(), (std::vector<std::string>{"keep move to -16 0 0"}));
    EXPECT_TRUE(bot.Mine().found);
    EXPECT_FALSE(bot.Mine().inPlace);
}

TEST(MemberSpotRule, ABotThatStaysGoesToItsSpotToo)
{
    MemberBot bot;

    EXPECT_EQ(bot.Think(Bots::CommandState::Stay), "go to the member spot");
}

TEST(MemberSpotRule, AtItsSpotTheBotIsInPlaceAndHoldsThere)
{
    MemberBot bot;
    bot.Think();
    bot.spots.fake.SetPosition(Self, {-16.5f, 0, 0});
    bot.spots.fake.ClearDeeds();

    // Not the follow of the player: the bot holds its spot.
    EXPECT_EQ(bot.Think(), "go to the member spot");

    EXPECT_TRUE(bot.spots.fake.Deeds().empty());
    EXPECT_TRUE(bot.Mine().inPlace);

    // In place, the follow rules and the corpse loot leave it there.
    bot.spots.fake.SetLootWork(true);
    bot.Think();
    EXPECT_TRUE(bot.spots.fake.Deeds().empty());
}

TEST(MemberSpotRule, ABotThatDrinksGetsUpAndGoesToItsSpotThenDrinksThere)
{
    MemberBot bot;
    bot.stack.Push(Bots::MakeRestStrategy());
    bot.spots.fake.SetMana(Self, 100, 600);
    bot.spots.fake.SetDrinking();

    EXPECT_EQ(bot.Think(), "go to the member spot");

    bot.spots.fake.SetPosition(Self, {-16, 0, 0});
    bot.spots.fake.SetDrinking(false);
    bot.Think();
    bot.spots.fake.ClearDeeds();
    EXPECT_EQ(bot.Think(), "drink");
}

TEST(MemberSpotRule, TheSecondRangedBotTakesASpotAwayFromTheFirst)
{
    MemberBot bot;
    bot.group.SetMemberSpot(Other, true, {-16, 0, 0});

    bot.Think();

    Bots::MemberSpot const mine = bot.Mine();
    ASSERT_TRUE(mine.found);
    EXPECT_GE(Distance3d(mine.spot, {-16, 0, 0}), Bots::MemberSpreadYards);
}

TEST(MemberSpotRule, WithNoSafeSpotTheBotFollowsAsBefore)
{
    MemberBot bot;
    bot.spots.fake.AddLavaFromX(-1000.0f);

    bot.Think();

    EXPECT_FALSE(bot.Mine().found);
    EXPECT_NE(bot.Think(), "go to the member spot");
}

TEST(MemberSpotRule, NotBeforeTheTankHasItsFightSpotNorOnceThePullHolds)
{
    MemberBot bot;
    bot.group.StartPull(Tank, Kobold);
    EXPECT_NE(bot.Think(), "go to the member spot");

    bot.group.SetPullSpot(Tank, {0, 0, 0});
    bot.group.HoldPull(Tank);
    EXPECT_NE(bot.Think(), "go to the member spot");
}

TEST(MemberSpotRule, ABotToldToComeDoesNotGo)
{
    MemberBot bot;

    EXPECT_NE(bot.Think(Bots::CommandState::Come), "go to the member spot");
}

TEST(MemberSpotRule, TheTankHasNoMemberSpot)
{
    EXPECT_EQ(Bots::MakeMemberSpotStrategy(BotRole::Tank), nullptr);
}

// The tank's wait for the group (TankWaitsForTheGroup).
namespace
{
    struct Waiting
    {
        FakeBot fake{Tank};
        Bots::GroupCoordinator group;

        Waiting()
        {
            fake.SetHealth(Tank, 600, 600);
            fake.SetHealth(Self, 600, 600);
            fake.SetHealth(Other, 600, 600);
            fake.SetHealth(Player, 600, 600);
            fake.SetGroupMembers({Self, Other, Player});
            group.NoteBot(Tank, 1000, true);
            group.NoteBot(Self, 1000, true);
            group.NoteBot(Other, 1000, true);
            group.StartPull(Tank, Kobold);
            group.SetPullSpot(Tank, {0, 0, 0});
            group.StartGroupWait(Tank, 1000);
        }

        bool Waits(std::int64_t nowMs)
        {
            Bots::BotContext context{fake, fake, group, Bots::CommandState::Follow, Player};
            context.nowMs = nowMs;
            return Bots::TankWaitsForTheGroup(context);
        }
    };
}

TEST(GroupWait, TheTankWaitsForEachBotUntilItIsInPlace)
{
    Waiting wait;
    EXPECT_TRUE(wait.Waits(1500));

    wait.group.SetMemberSpot(Self, true, {-16, 0, 0});
    wait.group.SetMemberInPlace(Self);
    EXPECT_TRUE(wait.Waits(1500));

    wait.group.SetMemberSpot(Other, true, {-22, 0, 0});
    wait.group.SetMemberInPlace(Other);
    EXPECT_FALSE(wait.Waits(1500));
}

TEST(GroupWait, ABotWithNoSpotIsNotWaitedFor)
{
    Waiting wait;
    wait.group.SetMemberSpot(Self, false, {});
    wait.group.SetMemberSpot(Other, false, {});

    EXPECT_FALSE(wait.Waits(1500));
}

TEST(GroupWait, NotForTheDeadNorForABotOutOfTheGroup)
{
    Waiting wait;
    wait.fake.SetLifeState(Self, Bots::LifeState::Dead);
    wait.fake.SetGroupMembers({Self, Player});

    EXPECT_FALSE(wait.Waits(1500));
}

// The review of jvbastel/WOWAI#100: the tank waited the full 10 s on bots
// that never go to a member spot.
TEST(GroupWait, NotForABotThatJoinsNoPull)
{
    Waiting wait;
    for (Bots::CommandState command : {Bots::CommandState::Come, Bots::CommandState::Follow})
    {
        FakeBot member{command == Bots::CommandState::Come ? Self : Other};
        member.SetHealth(member.GetSelf(), 600, 600);
        if (command == Bots::CommandState::Follow)
            member.SetInCombat(Other);
        Bots::BotContext context{member, member, wait.group, command, Player};
        context.nowMs = 1200;
        Bots::NoteForPulls(context);
    }

    EXPECT_FALSE(wait.Waits(1500));
}

TEST(GroupWait, NotForABotThatStoppedThinking)
{
    Waiting wait;
    wait.group.SetMemberSpot(Self, true, {-16, 0, 0});
    wait.group.SetMemberInPlace(Self);

    EXPECT_TRUE(wait.Waits(1000 + Bots::BotNoteMs));
    // Other noted itself last at 1000: gone.
    EXPECT_FALSE(wait.Waits(1000 + Bots::BotNoteMs + 1));
}

TEST(GroupWait, TheWaitEndsAfterTenSeconds)
{
    Waiting wait;
    wait.group.NoteBot(Self, 1000 + Bots::GroupWaitMs - 1, true);

    EXPECT_TRUE(wait.Waits(1000 + Bots::GroupWaitMs - 1));
    EXPECT_FALSE(wait.Waits(1000 + Bots::GroupWaitMs));
}

TEST(GroupWait, ABotOnceInPlaceIsNotWaitedForAgain)
{
    Waiting wait;
    wait.group.SetMemberSpot(Self, true, {-16, 0, 0});
    wait.group.SetMemberInPlace(Self);
    wait.group.SetMemberSpot(Other, true, {-22, 0, 0});
    wait.group.SetMemberInPlace(Other);

    // Self walks off its spot (a fight, a push): the pull goes on.
    wait.fake.SetPosition(Self, {-40, 0, 0});

    EXPECT_FALSE(wait.Waits(1500));
}

TEST(GroupWait, ANewPullForgetsTheMemberSpots)
{
    Waiting wait;
    wait.group.SetMemberSpot(Self, true, {-16, 0, 0});

    wait.group.StartPull(Tank, Kobold);

    EXPECT_TRUE(wait.group.GetMemberSpots().empty());
}

TEST(GroupWait, TheWaitLineNamesTheCount)
{
    EXPECT_EQ(Bots::GroupWaitLine(1), "Waiting for 1 member to get in place.");
    EXPECT_EQ(Bots::GroupWaitLine(2), "Waiting for 2 members to get in place.");
}
