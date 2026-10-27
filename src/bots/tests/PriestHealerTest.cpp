#include "ClassModules.h"
#include "PriestModule.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>

using Bots::BotContext;
using Bots::GroupCoordinator;
using Bots::NoGuid;
using Bots::StrategyStack;
using Bots::Tests::FakeBot;
namespace Priest = Bots::Priest;

namespace
{
    Bots::Guid const Healer = 0x0000000000000012;
    Bots::Guid const Tank = 0x0000000000000011;
    Bots::Guid const Mage = 0x0000000000000013;
    Bots::Guid const Kobold = 0xF130000302000022;

    std::string CastOn(Bots::SpellId spell, Bots::Guid target)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "cast %u on 0x%016llx", spell, static_cast<unsigned long long>(target));
        return text;
    }

    bool EndsWith(std::string const& text, std::string const& end)
    {
        return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0;
    }

    // A level 16 Troll Priest healer in a group with a tank and a mage, all
    // at full health, with its buffs on everyone.
    struct PriestHealer
    {
        FakeBot fake{Healer};
        GroupCoordinator group;
        StrategyStack stack = Bots::MakeStrategyStack(Bots::BotClass::Priest, Bots::BotRole::Healer);
        // The clock of the think (BotContext::nowMs).
        std::int64_t nowMs = 0;

        PriestHealer()
        {
            for (Bots::SpellId spell : {Priest::LesserHeal, Priest::Heal, Priest::Renew, Priest::PowerWordShield,
                     Priest::PowerWordFortitude, Priest::InnerFire, Priest::Fade})
                fake.SetKnownSpell(spell);
            fake.SetGroupMembers({Tank, Mage});
            for (Bots::Guid member : {Healer, Tank, Mage})
            {
                fake.SetHealth(member, 600, 600);
                fake.SetDistance(member, 20.0f);
                fake.SetAura(member, Priest::PowerWordFortitude);
            }
            fake.SetDistance(Healer, 0.0f);
            fake.SetAura(Healer, Priest::InnerFire);
            fake.SetPower(Healer, 400, 400);
            fake.SetHealth(Kobold, 200, 200);
        }

        std::string Think(Bots::CommandState command = Bots::CommandState::None, Bots::Guid player = NoGuid)
        {
            BotContext context{fake, fake, group, command, player};
            context.nowMs = nowMs;
            return stack.Think(context);
        }

        // The tank fights the kobold.
        void InFight()
        {
            group.SetFocusTarget(Kobold);
            fake.SetVictim(Tank, Kobold);
            fake.SetVictim(Kobold, Tank);
            fake.SetInCombat(Healer);
        }
    };
}

TEST(PriestHealer, HealsAHurtTankWithHeal)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetHealth(Tank, 250, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::Heal, Tank)}));
}

TEST(PriestHealer, HealsTheMostHurtMemberFirst)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetHealth(Tank, 250, 600);
    priest.fake.SetHealth(Mage, 100, 300);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::Heal, Mage)}));
}

TEST(PriestHealer, HealsWithLesserHealWhenItCannotCastHeal)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetHealth(Tank, 250, 600);
    priest.fake.SetCannotCast(Priest::Heal);
    Bots::SpellId const LesserHeal3 = 2053;
    priest.fake.SetSpellChain({Priest::LesserHeal, 2052, LesserHeal3});
    priest.fake.SetKnownSpell(2052);
    priest.fake.SetKnownSpell(LesserHeal3);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(LesserHeal3, Tank)}));
}

TEST(PriestHealer, DoesNotTryToCastWhileStunned)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetHealth(Tank, 250, 600);
    priest.Think();
    ASSERT_FALSE(priest.fake.Deeds().empty()) << "without the lock the priest casts";
    priest.fake.ClearDeeds();
    priest.fake.SetStunned();

    priest.Think();

    for (std::string const& deed : priest.fake.Deeds())
        EXPECT_EQ(deed.find("cast"), std::string::npos) << deed;
}

TEST(PriestHealer, DoesNotTryToCastWhileSilenced)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetHealth(Tank, 100, 600);
    priest.fake.SetAura(Tank, Priest::PowerWordFortitude, false);
    priest.Think();
    ASSERT_FALSE(priest.fake.Deeds().empty()) << "without the lock the priest casts";
    priest.fake.ClearDeeds();
    priest.fake.SetSilenced();

    priest.Think();

    for (std::string const& deed : priest.fake.Deeds())
        EXPECT_EQ(deed.find("cast"), std::string::npos) << deed;
}

TEST(PriestHealer, ShieldsAMemberNearDeath)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetHealth(Tank, 150, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::PowerWordShield, Tank)}));
}

TEST(PriestHealer, HealsAMemberNearDeathWithWeakenedSoul)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetHealth(Tank, 150, 600);
    priest.fake.SetAura(Tank, Priest::WeakenedSoul);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::Heal, Tank)}));
}

TEST(PriestHealer, RenewsALightlyHurtMember)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetHealth(Tank, 500, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::Renew, Tank)}));
}

TEST(PriestHealer, LetsRenewWorkOnALightlyHurtMember)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetHealth(Tank, 500, 600);
    priest.fake.SetAura(Tank, Priest::Renew);

    EXPECT_EQ(priest.Think(), "");
    EXPECT_TRUE(priest.fake.Deeds().empty());
}

TEST(PriestHealer, HealsAModeratelyHurtMemberWithLesserHeal)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetHealth(Tank, 380, 600);
    priest.fake.SetAura(Tank, Priest::Renew);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::LesserHeal, Tank)}));
}

TEST(PriestHealer, HealsThroughADebuff)
{
    // A damage over time spell on the tank, which no level 16 group member
    // can remove: the Priest heals the damage as it comes.
    Bots::SpellId const Rend = 772;
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetAura(Tank, Rend);

    std::vector<std::string> deeds;
    for (std::uint32_t health : {520u, 380u, 250u, 150u})
    {
        priest.fake.SetHealth(Tank, health, 600);
        priest.Think();
        priest.fake.SetAura(Tank, Priest::Renew);
    }

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::Renew, Tank),
        CastOn(Priest::LesserHeal, Tank), CastOn(Priest::Heal, Tank), CastOn(Priest::PowerWordShield, Tank)}));
}

TEST(PriestHealer, FadesWhenAnEnemyAttacksIt)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetVictim(Kobold, Healer);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::Fade, Healer)}));
}

TEST(PriestHealer, ShieldsItselfWhenAnEnemyAttacksIt)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetVictim(Kobold, Healer);
    priest.fake.SetCannotCast(Priest::Fade);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::PowerWordShield, Healer)}));
}

TEST(PriestHealer, NeverAttacksAnEnemyThatAttacksIt)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetVictim(Kobold, Healer);
    priest.fake.SetCannotCast(Priest::Fade);
    priest.fake.SetAura(Healer, Priest::WeakenedSoul);

    for (int think = 0; think < 5; ++think)
        priest.Think();
    priest.fake.SetHealth(Healer, 200, 600);
    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::Heal, Healer)}));
}

TEST(PriestHealer, NeverActsOnAnEnemy)
{
    // Every spell is ready, the group is hurt in every way, and enemies
    // attack each member: each deed is a spell on a group member.
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetVictim(Kobold, Healer);
    std::vector<Bots::Guid> const members = {Healer, Tank, Mage};
    for (std::uint32_t health : {600u, 500u, 380u, 250u, 150u, 50u})
        for (Bots::Guid member : members)
        {
            priest.fake.SetHealth(member, health, 600);
            priest.Think();
        }

    ASSERT_FALSE(priest.fake.Deeds().empty());
    for (std::string const& deed : priest.fake.Deeds())
    {
        bool onMember = false;
        for (Bots::Guid member : members)
            onMember = onMember || EndsWith(deed, CastOn(0, member).substr(6));
        EXPECT_TRUE(deed.compare(0, 5, "cast ") == 0 && onMember) << deed;
    }
}

TEST(PriestHealer, GoesToAHurtMemberOutOfHealReach)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetHealth(Tank, 250, 600);
    priest.fake.SetDistance(Tank, 45.0f);
    // The real adapter refuses a spell on a unit out of range.
    for (Bots::SpellId spell : {Priest::Heal, Priest::LesserHeal, Priest::Renew, Priest::PowerWordShield})
        priest.fake.SetCannotCast(spell);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{"follow 0x0000000000000011"}));
}

TEST(PriestHealer, BuffsTheGroupWithFortitudeOutOfCombat)
{
    PriestHealer priest;
    priest.fake.SetAura(Mage, Priest::PowerWordFortitude, false);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::PowerWordFortitude, Mage)}));
}

TEST(PriestHealer, BuffsALowLevelMemberWithTheHighestRankItsLevelAllows)
{
    // Fortitude rank 2 (1244) and rank 3 (1245) are too high for a level 1
    // player: the game refuses them with SPELL_FAILED_LOWLEVEL.
    Bots::SpellId const Rank2 = 1244;
    Bots::SpellId const Rank3 = 1245;
    PriestHealer priest;
    priest.fake.SetSpellChain({Priest::PowerWordFortitude, Rank2, Rank3});
    priest.fake.SetKnownSpell(Rank2);
    priest.fake.SetKnownSpell(Rank3);
    priest.fake.SetLevelTooLowFor(Rank2, Mage);
    priest.fake.SetLevelTooLowFor(Rank3, Mage);
    priest.fake.SetAura(Mage, Priest::PowerWordFortitude, false);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::PowerWordFortitude, Mage)}));
}

TEST(PriestHealer, BuffsAMemberOfHighEnoughLevelWithTheHighestKnownRank)
{
    Bots::SpellId const Rank2 = 1244;
    PriestHealer priest;
    priest.fake.SetSpellChain({Priest::PowerWordFortitude, Rank2});
    priest.fake.SetKnownSpell(Rank2);
    priest.fake.SetLevelTooLowFor(Rank2, Tank);
    priest.fake.SetAura(Mage, Priest::PowerWordFortitude, false);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Rank2, Mage)}));
}

TEST(PriestHealer, PutsOnInnerFireOutOfCombat)
{
    PriestHealer priest;
    priest.fake.SetAura(Healer, Priest::InnerFire, false);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::InnerFire, Healer)}));
}

TEST(PriestHealer, SavesItsManaForHealsInAFight)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetAura(Mage, Priest::PowerWordFortitude, false);
    priest.fake.SetAura(Healer, Priest::InnerFire, false);

    EXPECT_EQ(priest.Think(), "");
}

TEST(PriestHealer, FollowsThePlayerWhenNobodyNeedsIt)
{
    Bots::Guid const Player = 0x05;
    PriestHealer priest;
    priest.fake.SetHealth(Player, 600, 600);
    priest.fake.SetDistance(Player, 30.0f);

    priest.Think(Bots::CommandState::Follow, Player);

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{"follow 0x0000000000000005"}));
}

TEST(PriestHealer, NamesItsSpellsByTheFirstRank)
{
    // The lines of the learnable spells of the 1.12.1 world database.
    std::ifstream file(std::string(BOT_TEST_DATA_DIR) + "/learnable-spells.tsv", std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    for (char const* line : {"139\tRenew\tRank 1", "17\tPower Word: Shield\tRank 1", "2050\tLesser Heal\tRank 1",
             "2054\tHeal\tRank 1", "1243\tPower Word: Fortitude\tRank 1", "588\tInner Fire\tRank 1",
             "586\tFade\tRank 1"})
        EXPECT_NE(text.str().find(std::string("\n") + line + "\n"), std::string::npos) << line;
}

// jvbastel/WOWAI#111: the healer waits HealDelayMs after the tank engages
// before its first heal, so that the tank gets threat on every enemy first.
// A member under 50% health is in danger: it gets a heal at once.
namespace
{
    // The tank pulls the kobold, and the kobold attacks the tank at
    // `engageMs`: the tank engages.
    void Engage(PriestHealer& priest, std::int64_t engageMs)
    {
        priest.group.StartPull(Tank, Kobold);
        priest.InFight();
        priest.group.HoldPull(Tank, engageMs);
        priest.nowMs = engageMs;
    }
}

TEST(PriestHealerDelay, DoesNotHealALightlyHurtTankRightAfterTheEngage)
{
    PriestHealer priest;
    Engage(priest, 1000);
    priest.fake.SetHealth(Tank, 400, 600);

    priest.nowMs = 1500;
    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{}));
}
