#include "BotGeometry.h"
#include "FightSpot.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

using Bots::FightSpot;
using Bots::FindFightSpot;
using Bots::Position;
using Bots::Tests::FakeBot;

// The caster pack of jvbastel/WOWAI#121, at pack9 of Ragefire Chasm: one
// Ragefire Shaman that stays at its spawn and casts. pack10 and pack11
// stand 16.8 and 13.9 yd from it. The positions are those of the route file
// (bots/data/routes/ragefire_chasm.txt). The line of sight is fake: a rock
// at x = -219 hides the way from p17 to near pull_pack8 from the shaman.
namespace
{
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Shaman = 0xF130002C0B00BCBE;   // pack9
    Bots::Guid const Shaman10 = 0xF130002C0B00BCB1; // pack10
    Bots::Guid const Trogg10 = 0xF130002C0A00BCB4;  // pack10
    Bots::Guid const Trogg11 = 0xF130002C0A00BCB2;  // pack11

    Position const P17 = {-205.46f, -31.92f, -49.76f};
    Position const PullPack8 = {-214.01f, -33.16f, -52.72f};
    Position const PullPack9 = {-225.85f, -32.92f, -55.42f};
    Position const Pack9 = {-253.89f, -38.88f, -60.46f};
    Position const Pack10 = {-266.73f, -28.03f, -60.61f};
    Position const Pack11 = {-266.61f, -44.40f, -60.85f};
    float const AggroYards = 15.0f;

    // The way of the full run, p17 to pull_pack8 to pull_pack9, a point
    // each 2 yards, oldest first.
    std::vector<Position> Way()
    {
        std::vector<Position> way;
        Position const corners[] = {P17, PullPack8, PullPack9};
        for (int leg = 0; leg < 2; ++leg)
        {
            Position const a = corners[leg], b = corners[leg + 1];
            int const steps = int(Bots::Distance3d(a, b) / 2.0f);
            for (int i = 0; i < steps; ++i)
            {
                float const t = float(i) / float(steps);
                way.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
            }
        }
        way.push_back(PullPack9);
        return way;
    }

    // The tank at pull_pack9 with the way behind it, pack9 to pack11 in
    // place, and the rock.
    struct CasterPull
    {
        FakeBot fake{Tank};
        std::vector<Position> trail = Way();

        // `rockX`: the x of the rock, or 0 for none.
        explicit CasterPull(float rockX = -219.0f)
        {
            fake.SetPosition(Tank, PullPack9);
            AddEnemy(Shaman, Pack9);
            AddEnemy(Shaman10, Pack10);
            AddEnemy(Trogg10, {Pack10.x - 2.0f, Pack10.y + 2.0f, Pack10.z});
            AddEnemy(Trogg11, Pack11);
            fake.SetMana(Shaman, 400, 400);
            fake.SetMana(Shaman10, 400, 400);
            if (rockX != 0.0f)
                fake.AddSightWall({rockX, -40.0f, 0.0f}, {rockX, -31.0f, 0.0f});
        }

        void AddEnemy(Bots::Guid enemy, Position at)
        {
            fake.SetPosition(enemy, at);
            fake.AddEnemy(enemy, at, AggroYards);
        }

        FightSpot Find() const { return FindFightSpot(fake, trail, Shaman); }
    };
}

// Plan item 2: the line-of-sight pull. The first spot back on the way that
// the shaman cannot see from its spawn, with the shot from pull_pack9
// (within 10 yd of it).
TEST(CasterPack, TheTankFightsACasterPackWhereTheCasterCannotSeeIt)
{
    CasterPull pull;

    FightSpot const spot = pull.Find();

    EXPECT_TRUE(spot.clean);
    EXPECT_TRUE(spot.lineOfSight);
    EXPECT_GT(spot.spot.x, -219.0f);
    EXPECT_LE(Bots::Distance2d(spot.spot, PullPack9), 10.0f);
    EXPECT_FALSE(spot.hasShootSpot);
}

// Plan item 1: a pack with no mana is no caster pack, and the tank fights
// where it stands, as before.
TEST(CasterPack, APackWithoutManaIsNoCasterPack)
{
    CasterPull pull;
    pull.fake.SetMana(Shaman, 0, 0);

    FightSpot const spot = pull.Find();

    EXPECT_TRUE(spot.clean);
    EXPECT_FALSE(spot.lineOfSight);
    EXPECT_EQ(spot.spot.x, PullPack9.x);
}

// Plan item 3: with no spot out of the caster's sight, the search judges
// the area round its spawn: pack11 (13.9 yd) and pack10 (16.8 yd) are in
// the aggro distance and margin there, so the pull warns of 2 packs.
TEST(CasterPack, WithNoSpotOutOfSightTheSpawnAreaCountsAndThePullWarns)
{
    CasterPull pull(0.0f);

    FightSpot const spot = pull.Find();

    EXPECT_FALSE(spot.lineOfSight);
    EXPECT_FALSE(spot.clean);
    EXPECT_EQ(spot.extraPacks, 2u);
}

// A shot within 10 yd of the spot is needed: a spot out of sight with the
// shot further away does not count.
TEST(CasterPack, ASpotOutOfSightNeedsAShotWithin10Yards)
{
    // The rock hides only p17 and the way next to it, 13 yd and more from
    // pull_pack9, where the tank shoots.
    CasterPull pull(-211.0f);
    // The shaman is out of shot from everywhere but pull_pack9.
    pull.fake.AddBlindToFromX(Shaman, -224.0f);

    FightSpot const spot = pull.Find();

    EXPECT_FALSE(spot.lineOfSight);
    EXPECT_FALSE(spot.clean);
}
