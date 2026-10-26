#include "ThreatWatch.h"

#include <gtest/gtest.h>

using Bots::EnemyOnMember;
using Bots::ThreatWatch;

// A threat loss: an enemy hits a group member who is not the tank for more
// than 4 seconds (spec section 10, pass thresholds).
namespace
{
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Rogue = 0x13;
    Bots::Guid const Enforcer = 0xF130000302000022;
    Bots::Guid const Other = 0xF130000302000033;
}

TEST(ThreatWatch, NoLossWithoutHits)
{
    ThreatWatch watch;
    watch.Update({}, 0);
    watch.Update({}, 10000);

    EXPECT_TRUE(watch.Losses().empty());
}

TEST(ThreatWatch, AShortHitIsNoLoss)
{
    ThreatWatch watch;
    watch.Update({{Enforcer, Healer}}, 0);
    watch.Update({{Enforcer, Healer}}, 4000);
    watch.Update({}, 4500);

    EXPECT_TRUE(watch.Losses().empty());
}

TEST(ThreatWatch, AHitOfMoreThanFourSecondsIsALoss)
{
    ThreatWatch watch;
    watch.Update({{Enforcer, Healer}}, 1000);
    watch.Update({{Enforcer, Healer}}, 5001);

    ASSERT_EQ(watch.Losses().size(), 1u);
    EXPECT_EQ(watch.Losses()[0].enemy, Enforcer);
    EXPECT_EQ(watch.Losses()[0].member, Healer);
    EXPECT_EQ(watch.Losses()[0].startMs, 1000);
}

TEST(ThreatWatch, ALongHitCountsOnceAndKeepsItsEnd)
{
    ThreatWatch watch;
    for (std::int64_t t = 0; t <= 120000; t += 500)
        watch.Update({{Enforcer, Healer}}, t);
    watch.Update({}, 120500);

    ASSERT_EQ(watch.Losses().size(), 1u);
    EXPECT_EQ(watch.Losses()[0].endMs, 120000);
}

TEST(ThreatWatch, AHitAgainAfterAGapStartsANewCount)
{
    ThreatWatch watch;
    watch.Update({{Enforcer, Healer}}, 0);
    watch.Update({{Enforcer, Healer}}, 3000);
    watch.Update({}, 3500);
    watch.Update({{Enforcer, Healer}}, 4000);
    watch.Update({{Enforcer, Healer}}, 7000);

    EXPECT_TRUE(watch.Losses().empty());
}

TEST(ThreatWatch, EachEnemyAndMemberCountsApart)
{
    ThreatWatch watch;
    watch.Update({{Enforcer, Healer}, {Other, Rogue}}, 0);
    watch.Update({{Enforcer, Rogue}, {Other, Rogue}}, 3000);
    watch.Update({{Enforcer, Rogue}, {Other, Rogue}}, 6000);

    ASSERT_EQ(watch.Losses().size(), 1u);
    EXPECT_EQ(watch.Losses()[0].enemy, Other);
}

// A lost mob: an enemy hits a group member (the tank too), and no bot
// attacks it, for more than the limit (10 seconds by default). It fails the
// scripted run.
namespace
{
    Bots::Guid const Tank = 0x11;
}

TEST(LostMobWatch, NoLostMobWithoutHits)
{
    Bots::LostMobWatch watch;
    watch.Update({}, {}, 0);
    watch.Update({}, {}, 20000);

    EXPECT_TRUE(watch.LostMobs().empty());
}

TEST(LostMobWatch, AnEnemyThatHitsAMemberUnattackedForTenSecondsIsLost)
{
    Bots::LostMobWatch watch;
    watch.Update({{Enforcer, Healer}}, {}, 0);
    watch.Update({{Enforcer, Healer}}, {}, 10000);
    EXPECT_TRUE(watch.LostMobs().empty());

    watch.Update({{Enforcer, Healer}}, {}, 10500);

    ASSERT_EQ(watch.LostMobs().size(), 1u);
    EXPECT_EQ(watch.LostMobs()[0].enemy, Enforcer);
    EXPECT_EQ(watch.LostMobs()[0].member, Healer);
    EXPECT_EQ(watch.LostMobs()[0].startMs, 0);
    EXPECT_EQ(watch.LostMobs()[0].endMs, 10500);
}

TEST(LostMobWatch, AnEnemyThatABotAttacksIsNotLost)
{
    Bots::LostMobWatch watch;
    for (std::int64_t now = 0; now <= 30000; now += 1000)
        watch.Update({{Enforcer, Healer}}, {Enforcer}, now);

    EXPECT_TRUE(watch.LostMobs().empty());
}

TEST(LostMobWatch, AnAttackStartsTheTimeAgain)
{
    Bots::LostMobWatch watch;
    watch.Update({{Enforcer, Healer}}, {}, 0);
    watch.Update({{Enforcer, Healer}}, {Enforcer}, 8000);
    watch.Update({{Enforcer, Healer}}, {}, 9000);
    watch.Update({{Enforcer, Healer}}, {}, 18000);

    EXPECT_TRUE(watch.LostMobs().empty());
}

TEST(LostMobWatch, TheEnemyCountsOnAnyMemberItHits)
{
    Bots::LostMobWatch watch;
    // It goes from the healer to the tank, and nobody attacks it. The tank
    // does not fight: it holds nothing.
    watch.Update({{Enforcer, Healer}}, {}, 0);
    watch.Update({{Enforcer, Tank}}, {}, 6000);
    watch.Update({{Enforcer, Tank}}, {}, 11000);

    ASSERT_EQ(watch.LostMobs().size(), 1u);
    EXPECT_EQ(watch.LostMobs()[0].member, Tank);
}

TEST(LostMobWatch, AnEnemyThatStopsItsHitsStartsTheTimeAgain)
{
    Bots::LostMobWatch watch;
    watch.Update({{Enforcer, Healer}}, {}, 0);
    watch.Update({}, {}, 5000);
    watch.Update({{Enforcer, Healer}}, {}, 6000);
    watch.Update({{Enforcer, Healer}}, {}, 15000);

    EXPECT_TRUE(watch.LostMobs().empty());
}

TEST(LostMobWatch, OneEnemyIsLostOnce)
{
    Bots::LostMobWatch watch;
    watch.Update({{Enforcer, Healer}, {Other, Rogue}}, {}, 0);
    watch.Update({{Enforcer, Healer}, {Other, Rogue}}, {Other}, 11000);
    watch.Update({{Enforcer, Healer}}, {}, 12000);

    ASSERT_EQ(watch.LostMobs().size(), 1u);
    EXPECT_EQ(watch.LostMobs()[0].enemy, Enforcer);
    EXPECT_EQ(watch.LostMobs()[0].endMs, 12000);
}

TEST(LostMobWatch, TheLimitIsASetting)
{
    Bots::LostMobWatch watch(3000);
    watch.Update({{Enforcer, Healer}}, {}, 0);
    watch.Update({{Enforcer, Healer}}, {}, 3500);

    EXPECT_EQ(watch.LostMobs().size(), 1u);
}

// The tank holds an add on itself while the group kills the focus: the add
// is in the fight, not lost.
TEST(LostMobWatch, AnEnemyOnTheTankThatFightsIsHeld)
{
    Bots::LostMobWatch watch;
    for (std::int64_t now = 0; now <= 20000; now += 1000)
        watch.Update({{Enforcer, Tank}}, {Other}, now, Tank);

    EXPECT_TRUE(watch.LostMobs().empty());
}

TEST(LostMobWatch, AHeldEnemyThatTurnsToTheHealerStartsItsTime)
{
    Bots::LostMobWatch watch;
    watch.Update({{Enforcer, Tank}}, {}, 0, Tank);
    watch.Update({{Enforcer, Healer}}, {}, 5000, Tank);
    watch.Update({{Enforcer, Healer}}, {}, 14000, Tank);
    EXPECT_TRUE(watch.LostMobs().empty());

    watch.Update({{Enforcer, Healer}}, {}, 15500, Tank);

    ASSERT_EQ(watch.LostMobs().size(), 1u);
    EXPECT_EQ(watch.LostMobs()[0].startMs, 5000);
}

// What counts as an attack for the lost-mob watch (WOWAI#83): a bot hits
// the enemy or casts at it. A selection is no attack, and the test driver
// is no bot: its attacks do not hold an enemy.
namespace
{
    Bots::MemberAttacks Selects(Bots::Guid enemy)
    {
        Bots::MemberAttacks member;
        member.selection = enemy;
        return member;
    }
}

TEST(AttackedEnemies, ASelectionIsNoAttack)
{
    EXPECT_TRUE(Bots::AttackedEnemies({Selects(Enforcer)}).empty());
}

TEST(LostMobWatch, AnEnemyThatABotOnlySelectsIsLost)
{
    Bots::LostMobWatch watch;
    for (std::int64_t now = 0; now <= Bots::LostMobMs; now += 1000)
        watch.Update({{Enforcer, Healer}}, Bots::AttackedEnemies({Selects(Enforcer)}), now);
    EXPECT_TRUE(watch.LostMobs().empty());

    watch.Update({{Enforcer, Healer}}, Bots::AttackedEnemies({Selects(Enforcer)}), Bots::LostMobMs + 1000);
    ASSERT_EQ(watch.LostMobs().size(), 1u);
    EXPECT_EQ(watch.LostMobs()[0].enemy, Enforcer);
}

TEST(AttackedEnemies, ABotKeepsTheTargetOfEachSpellItCasts)
{
    Bots::MemberAttacks hunter;
    hunter.spellTargets = {Enforcer, Other};

    EXPECT_EQ(Bots::AttackedEnemies({hunter}), (std::vector<Bots::Guid>{Enforcer, Other}));
}

TEST(AttackedEnemies, ABotAttacksTheEnemyItHitsOrCastsAt)
{
    Bots::MemberAttacks hits, casts, pet;
    hits.victim = Enforcer;
    casts.spellTargets = {Other};
    casts.selection = Other;
    pet.petVictim = Enforcer;

    EXPECT_EQ(Bots::AttackedEnemies({hits}), std::vector<Bots::Guid>{Enforcer});
    EXPECT_EQ(Bots::AttackedEnemies({casts}), std::vector<Bots::Guid>{Other});
    EXPECT_EQ(Bots::AttackedEnemies({pet}), std::vector<Bots::Guid>{Enforcer});
}

TEST(LostMobWatch, TheTestDriverDoesNotHoldAnEnemy)
{
    Bots::MemberAttacks driver;
    driver.bot = false;
    driver.victim = Enforcer;
    driver.spellTargets = {Enforcer};
    driver.petVictim = Enforcer;
    driver.selection = Enforcer;
    Bots::LostMobWatch watch;
    for (std::int64_t now = 0; now <= 11000; now += 1000)
        watch.Update({{Enforcer, Healer}}, Bots::AttackedEnemies({driver, Selects(Enforcer)}), now);

    ASSERT_EQ(watch.LostMobs().size(), 1u);
    EXPECT_EQ(watch.LostMobs()[0].enemy, Enforcer);
}

// Each threat loss and lost mob keeps its own name (WOWAI#87): the watch
// asks for it once, when the loss starts, so a later loss or the end of the
// enemy does not change it.
namespace
{
    std::string NameOf(EnemyOnMember const& hit)
    {
        return std::string(hit.enemy == Enforcer ? "Enforcer" : "Other") + " on "
            + (hit.member == Healer ? "Healer" : "Rogue");
    }
}

TEST(ThreatWatch, EachLossKeepsTheNameOfItsHit)
{
    ThreatWatch watch;
    int asked = 0;
    auto const name = [&](EnemyOnMember const& hit) { ++asked; return NameOf(hit); };
    watch.Update({{Enforcer, Healer}}, 0, name);
    watch.Update({{Enforcer, Healer}, {Other, Rogue}}, 5000, name);
    watch.Update({{Enforcer, Healer}, {Other, Rogue}}, 10000, name);

    ASSERT_EQ(watch.Losses().size(), 2u);
    EXPECT_EQ(watch.Losses()[0].name, "Enforcer on Healer");
    EXPECT_EQ(watch.Losses()[1].name, "Other on Rogue");
    EXPECT_EQ(asked, 2);
}

TEST(LostMobWatch, EachLostMobKeepsTheNameOfItsFirstHit)
{
    Bots::LostMobWatch watch;
    int asked = 0;
    auto const name = [&](EnemyOnMember const& hit) { ++asked; return NameOf(hit); };
    watch.Update({{Enforcer, Healer}}, {}, 0, Bots::NoGuid, name);
    watch.Update({{Enforcer, Healer}}, {}, 10500, Bots::NoGuid, name);
    watch.Update({{Enforcer, Rogue}}, {}, 11000, Bots::NoGuid, name);

    ASSERT_EQ(watch.LostMobs().size(), 1u);
    EXPECT_EQ(watch.LostMobs()[0].name, "Enforcer on Healer");
    EXPECT_EQ(asked, 1);
}

// One text for the threat losses and the lost mobs of the result line.
TEST(HitText, ANameAndItsWholeSeconds)
{
    EXPECT_EQ(Bots::HitText(Bots::LostMob{Enforcer, Healer, 1000, 12900, "Enforcer on Healer"}), "Enforcer on Healer 11 s");
}

TEST(HitText, TheCountAndEachHitOfAList)
{
    std::vector<Bots::ThreatLoss> const losses = {
        {Enforcer, Healer, 0, 5000, "Enforcer on Healer"}, {Other, Rogue, 1000, 8000, "Other on Rogue"}};

    EXPECT_EQ(Bots::HitListText("threat losses", losses),
        ", threat losses 2 (Enforcer on Healer 5 s; Other on Rogue 7 s)");
    EXPECT_EQ(Bots::HitListText("lost mobs", std::vector<Bots::LostMob>{}), ", lost mobs 0");
}
