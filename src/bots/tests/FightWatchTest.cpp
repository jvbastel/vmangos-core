#include "FightWatch.h"

#include <gtest/gtest.h>

using Bots::CastKind;
using Bots::FightMemberSeen;
using Bots::FightSummary;
using Bots::FightWatch;
using Bots::Guid;

namespace
{
    Guid const Grukk = 11;
    Guid const Hela = 12;
    Guid const Borin = 13;
    Guid const Earthborer = 500;
    Guid const OtherMob = 501;

    FightWatch StartedFight()
    {
        FightWatch watch;
        watch.Start(3, "pack12", false, 10000, {{"Grukk", Grukk}, {"Hela", Hela}, {"Borin", Borin}});
        return watch;
    }

    FightMemberSeen Seen(std::string const& name, Guid victim = Bots::NoGuid, Bots::SpellId spell = Bots::NoSpell,
        Guid spellTarget = Bots::NoGuid, CastKind kind = CastKind::None)
    {
        return {name, victim, spell, spellTarget, kind};
    }

    Bots::FightMember const& MemberOf(FightSummary const& summary, std::string const& name)
    {
        for (Bots::FightMember const& member : summary.members)
            if (member.name == name)
                return member;
        throw std::runtime_error("no member " + name);
    }
}

TEST(FightWatch, AMemberThatOnlyFollowsDidNotJoin)
{
    FightWatch watch = StartedFight();
    watch.Observe(11000, true, {Earthborer}, {Seen("Grukk", Earthborer), Seen("Borin")});
    watch.Observe(12000, true, {Earthborer}, {Seen("Grukk", Earthborer), Seen("Borin")});

    FightSummary const summary = watch.End(20000);

    EXPECT_TRUE(MemberOf(summary, "Grukk").joined);
    EXPECT_FALSE(MemberOf(summary, "Borin").joined);
    EXPECT_EQ(MemberOf(summary, "Borin").casts, 0u);
}

TEST(FightWatch, AMeleeMemberWithOnlyAVictimTookPartFromItsFirstSwing)
{
    FightWatch watch = StartedFight();
    watch.Observe(11000, true, {Earthborer}, {Seen("Borin")});
    watch.Observe(12500, true, {Earthborer}, {Seen("Borin", Earthborer)});
    watch.Observe(13000, true, {Earthborer}, {Seen("Borin", Earthborer)});

    Bots::FightMember const borin = MemberOf(watch.End(20000), "Borin");

    EXPECT_TRUE(borin.joined);
    EXPECT_EQ(borin.firstActionMs, 2500);
    EXPECT_EQ(borin.casts, 0u);
}

TEST(FightWatch, AHealerThatOnlyHealsTookPartAndCountsEachHeal)
{
    FightWatch watch = StartedFight();
    watch.Observe(11000, true, {Earthborer}, {Seen("Hela", Bots::NoGuid, 2054, Grukk, CastKind::Heal)});
    watch.Observe(12000, true, {Earthborer}, {Seen("Hela", Bots::NoGuid, 2054, Grukk, CastKind::Heal)});
    watch.Observe(13000, true, {Earthborer}, {Seen("Hela")});
    watch.Observe(14000, true, {Earthborer}, {Seen("Hela", Bots::NoGuid, 2054, Hela, CastKind::Heal)});

    Bots::FightMember const hela = MemberOf(watch.End(20000), "Hela");

    EXPECT_TRUE(hela.joined);
    EXPECT_EQ(hela.firstActionMs, 1000);
    EXPECT_EQ(hela.casts, 2u);
}

TEST(FightWatch, ABuffAtAMemberOrACastAtAnotherMobIsNoPart)
{
    FightWatch watch = StartedFight();
    watch.Observe(11000, true, {Earthborer}, {Seen("Hela", Bots::NoGuid, 1243, Grukk, CastKind::Other),
        Seen("Borin", Bots::NoGuid, 133, OtherMob, CastKind::Harm)});

    FightSummary const summary = watch.End(20000);

    EXPECT_FALSE(MemberOf(summary, "Hela").joined);
    EXPECT_FALSE(MemberOf(summary, "Borin").joined);
}

TEST(FightWatch, ACastAtAMobBeforeItJoinsTheFightCounts)
{
    // The opening cast of a caster comes before the mob attacks anyone.
    FightWatch watch = StartedFight();
    watch.Observe(10500, false, {}, {Seen("Borin", Bots::NoGuid, 133, Earthborer, CastKind::Harm)});
    watch.Observe(12000, true, {Earthborer}, {Seen("Borin")});

    Bots::FightMember const borin = MemberOf(watch.End(20000), "Borin");

    EXPECT_TRUE(borin.joined);
    EXPECT_EQ(borin.firstActionMs, 500);
    EXPECT_EQ(borin.casts, 1u);
}

TEST(FightWatch, TheFightIsOverWhenTheGroupLeavesCombat)
{
    FightWatch watch = StartedFight();

    EXPECT_FALSE(watch.Observe(11000, false, {}, {}));
    EXPECT_FALSE(watch.Observe(12000, true, {Earthborer}, {}));
    EXPECT_FALSE(watch.Observe(13000, true, {Earthborer}, {}));
    EXPECT_TRUE(watch.Observe(14000, false, {}, {}));
    EXPECT_TRUE(watch.Running());

    FightSummary const summary = watch.End(14000);
    EXPECT_FALSE(watch.Running());
    EXPECT_EQ(summary.startMs, 10000);
    EXPECT_EQ(summary.endMs, 14000);
}

TEST(FightWatch, NoFightRunsBeforeAPull)
{
    FightWatch watch;

    EXPECT_FALSE(watch.Running());
    EXPECT_FALSE(watch.Observe(11000, true, {Earthborer}, {}));
    EXPECT_FALSE(watch.Observe(12000, false, {}, {}));
}

TEST(FightWatch, TheTextSaysWhenEachMemberJoinedAndWhoDidNot)
{
    FightSummary summary;
    summary.number = 3;
    summary.pack = "pack12";
    summary.members = {{"Grukk", true, 400, 1}, {"Hela", true, 2100, 5}, {"Borin", false, 0, 0}};

    EXPECT_EQ(Bots::FightText(summary), "fight 3 pack12: Grukk 0.4 s 1 cast, Hela 2.1 s 5 casts, Borin did not join");

    summary.pack.clear();
    summary.again = true;
    summary.members.resize(1);
    EXPECT_EQ(Bots::FightText(summary), "fight 3 pull again: Grukk 0.4 s 1 cast");
}
