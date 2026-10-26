#include "GroupCoordinator.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

using Bots::GroupCoordinator;
using Bots::NoGuid;

// The interrupt claims of the group coordinator: two bots never interrupt
// the same cast.
namespace
{
    Bots::Guid const Rogue = 0x13;
    Bots::Guid const Warrior = 0x11;
    Bots::Guid const Shaman = 0xF130000302000022;
    Bots::Guid const Warlock = 0xF130000302000033;
    Bots::SpellId const HealingWave = 11986;
    Bots::SpellId const LightningBolt = 9532;
}

TEST(InterruptClaim, ACastHasNoClaimAtFirst)
{
    GroupCoordinator group;

    EXPECT_EQ(group.GetInterruptClaim(Shaman, HealingWave), NoGuid);
}

TEST(InterruptClaim, TheFirstBotGetsTheCast)
{
    GroupCoordinator group;

    EXPECT_TRUE(group.ClaimInterrupt(Shaman, HealingWave, Rogue));

    EXPECT_EQ(group.GetInterruptClaim(Shaman, HealingWave), Rogue);
}

TEST(InterruptClaim, ASecondBotDoesNotGetTheSameCast)
{
    GroupCoordinator group;
    group.ClaimInterrupt(Shaman, HealingWave, Rogue);

    EXPECT_FALSE(group.ClaimInterrupt(Shaman, HealingWave, Warrior));
    EXPECT_EQ(group.GetInterruptClaim(Shaman, HealingWave), Rogue);
}

TEST(InterruptClaim, TheBotWithTheClaimKeepsIt)
{
    // For example, its Kick failed, and it tries again.
    GroupCoordinator group;
    group.ClaimInterrupt(Shaman, HealingWave, Rogue);

    EXPECT_TRUE(group.ClaimInterrupt(Shaman, HealingWave, Rogue));
}

TEST(InterruptClaim, CastsOfTwoEnemiesAreTwoClaims)
{
    GroupCoordinator group;
    group.ClaimInterrupt(Shaman, HealingWave, Rogue);

    EXPECT_TRUE(group.ClaimInterrupt(Warlock, LightningBolt, Warrior));
    EXPECT_EQ(group.GetInterruptClaim(Warlock, LightningBolt), Warrior);
}

TEST(InterruptClaim, ANewSpellOfTheCasterIsANewCast)
{
    // The old claim is of a cast that ended.
    GroupCoordinator group;
    group.ClaimInterrupt(Shaman, HealingWave, Rogue);

    EXPECT_EQ(group.GetInterruptClaim(Shaman, LightningBolt), NoGuid);
    EXPECT_TRUE(group.ClaimInterrupt(Shaman, LightningBolt, Warrior));
    EXPECT_EQ(group.GetInterruptClaim(Shaman, HealingWave), NoGuid);
}

TEST(InterruptClaim, AReleasedCastIsFreeAgain)
{
    GroupCoordinator group;
    group.ClaimInterrupt(Shaman, HealingWave, Rogue);

    group.ReleaseInterrupt(Shaman);

    EXPECT_EQ(group.GetInterruptClaim(Shaman, HealingWave), NoGuid);
    EXPECT_TRUE(group.ClaimInterrupt(Shaman, HealingWave, Warrior));
}

TEST(InterruptClaim, ListsTheClaims)
{
    GroupCoordinator group;
    group.ClaimInterrupt(Shaman, HealingWave, Rogue);

    std::vector<Bots::InterruptClaim> const claims = group.GetInterruptClaims();

    ASSERT_EQ(claims.size(), 1u);
    EXPECT_EQ(claims[0].caster, Shaman);
    EXPECT_EQ(claims[0].spell, HealingWave);
    EXPECT_EQ(claims[0].bot, Rogue);
}

TEST(InterruptClaim, OnlyOneOfTwoBotsOnTwoMapThreadsGetsTheCast)
{
    for (int round = 0; round < 2000; ++round)
    {
        GroupCoordinator group;
        std::atomic<int> winners{0};
        std::thread warrior([&] { winners += group.ClaimInterrupt(Shaman, HealingWave, Warrior) ? 1 : 0; });
        winners += group.ClaimInterrupt(Shaman, HealingWave, Rogue) ? 1 : 0;
        warrior.join();
        ASSERT_EQ(winners.load(), 1);
    }
}
