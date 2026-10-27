#include "WispTrail.h"

#include <gtest/gtest.h>

#include <set>

using Bots::Guid;
using Bots::WispTrail;

namespace
{
    // The wisps that are still on the map.
    struct Map
    {
        std::set<Guid> wisps;
        Guid lastGuid = 0;
        bool operator()(Guid guid) const { return wisps.count(guid) != 0; }
    };
}

TEST(WispTrail, AnEmptyTrailSummonsANewWisp)
{
    WispTrail trail;
    Map map;
    WispTrail::Choice const choice = trail.Next(map, 100);
    EXPECT_EQ(WispTrail::Action::Summon, choice.action);
}

namespace
{
    // Places `count` wisps with the trail, as the walk does: a summon makes
    // the next guid. Gives back the guid of each place.
    std::vector<Guid> Place(WispTrail& trail, Map& map, std::size_t count, std::size_t summonLimit = 100)
    {
        std::vector<Guid> placed;
        for (std::size_t k = 0; k < count; ++k)
        {
            std::size_t const used = map.wisps.size();
            WispTrail::Choice const choice = trail.Next(map, used < summonLimit ? summonLimit - used : 0);
            if (choice.action == WispTrail::Action::Summon)
            {
                Guid const guid = ++map.lastGuid;
                map.wisps.insert(guid);
                trail.Summoned(guid);
                placed.push_back(guid);
            }
            else
                placed.push_back(choice.guid);
        }
        return placed;
    }
}

TEST(WispTrail, Wisp101IsWisp1MovedAndTheTrailSummonsAtMost100)
{
    WispTrail trail;
    Map map;
    std::vector<Guid> const placed = Place(trail, map, 300);
    EXPECT_EQ(100u, map.wisps.size());
    EXPECT_EQ(placed[0], placed[100]);
    EXPECT_EQ(placed[1], placed[101]);
    EXPECT_EQ(placed[99], placed[199]);
    EXPECT_EQ(placed[0], placed[200]);
    EXPECT_EQ(placed[299], trail.Wisps().back());
    EXPECT_EQ(placed[200], trail.Wisps().front());
}

TEST(WispTrail, AWispThatTimedOutDropsOutAndANewOneIsSummonedInItsPlace)
{
    WispTrail trail;
    Map map;
    std::vector<Guid> const placed = Place(trail, map, 100);
    map.wisps.erase(placed[0]);
    map.wisps.erase(placed[1]);

    WispTrail::Choice const choice = trail.Next(map, 2);
    EXPECT_EQ(WispTrail::Action::Summon, choice.action);
    trail.Summoned(500);
    EXPECT_EQ(99u, trail.Wisps().size());
    EXPECT_EQ(placed[2], trail.Wisps().front());
    EXPECT_EQ(Guid(500), trail.Wisps().back());
}

TEST(WispTrail, WithNoFreeSummonTheOldestWispThatIsStillOnTheMapMoves)
{
    WispTrail trail;
    Map map;
    std::vector<Guid> const placed = Place(trail, map, 5);
    map.wisps.erase(placed[0]);

    WispTrail::Choice const choice = trail.Next(map, 0);
    EXPECT_EQ(WispTrail::Action::Move, choice.action);
    EXPECT_EQ(placed[1], choice.guid);
    EXPECT_EQ(std::vector<Guid>({placed[2], placed[3], placed[4], placed[1]}), trail.Wisps());
}

TEST(WispTrail, TheSummonLimitOfTheCharacterKeepsTheTrailShorter)
{
    WispTrail trail;
    Map map;
    std::vector<Guid> const placed = Place(trail, map, 40, 30);
    EXPECT_EQ(30u, map.wisps.size());
    EXPECT_EQ(placed[0], placed[30]);
}

TEST(WispTrail, NoWispAndNoFreeSummonIsNothingToDo)
{
    WispTrail trail;
    Map map;
    EXPECT_EQ(WispTrail::Action::None, trail.Next(map, 0).action);
}

TEST(WispTrail, ClearGivesBackTheWispsAndEmptiesTheTrail)
{
    WispTrail trail;
    Map map;
    std::vector<Guid> const placed = Place(trail, map, 3);
    EXPECT_EQ(placed, trail.Clear());
    EXPECT_TRUE(trail.Wisps().empty());
    EXPECT_EQ(WispTrail::Action::Summon, trail.Next(map, 97).action);
}
