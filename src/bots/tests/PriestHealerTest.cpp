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

    // A level 16 Troll Priest healer in a group with a warrior tank (rage)
    // and a mage, all at full health, with its buffs on everyone.
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
            group.SetTank(Tank);
            fake.SetPowerKind(Tank, Bots::PowerKind::Rage);
            fake.SetPowerKind(Healer, Bots::PowerKind::Mana);
            fake.SetPowerKind(Mage, Bots::PowerKind::Mana);
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
    priest.fake.SetHealth(Mage, 80, 300);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::PowerWordShield, Mage)}));
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
        CastOn(Priest::LesserHeal, Tank), CastOn(Priest::Heal, Tank), CastOn(Priest::Heal, Tank)}));
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
             "586\tFade\tRank 1", "2061\tFlash Heal\tRank 1"})
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

TEST(PriestHealerDelay, HealsAMemberInDangerAtOnce)
{
    PriestHealer priest;
    Engage(priest, 1000);
    priest.fake.SetHealth(Tank, 250, 600);

    priest.nowMs = 1100;
    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::Heal, Tank)}));
}

TEST(PriestHealerDelay, HealsALightlyHurtTankTwoSecondsAfterTheEngage)
{
    PriestHealer priest;
    Engage(priest, 1000);
    priest.fake.SetHealth(Tank, 400, 600);

    priest.nowMs = 3000;
    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::Renew, Tank)}));
}

TEST(PriestHealerDelay, TheDelayStartsWhenTheTankEngagesNotAtThePullCommand)
{
    // The tank waits for the group (MemberSpot.h) for 10 seconds before
    // the kobold attacks it.
    PriestHealer priest;
    priest.group.StartPull(Tank, Kobold);
    priest.InFight();
    priest.fake.SetHealth(Tank, 400, 600);
    priest.nowMs = 10000;
    priest.Think();
    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{}));

    priest.group.HoldPull(Tank, 10000);
    priest.nowMs = 11500;
    priest.Think();
    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{}));

    priest.nowMs = 12000;
    priest.Think();
    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::Renew, Tank)}));
}

TEST(PriestHealerDelay, DoesNotWaitWhenThePullTargetAttacksAnotherMember)
{
    // The kobold attacks the mage, not the tank: the tank never engages.
    PriestHealer priest;
    priest.group.StartPull(Tank, Kobold);
    priest.InFight();
    priest.fake.SetVictim(Kobold, Mage);
    priest.fake.SetHealth(Mage, 400, 600);
    priest.nowMs = 5000;

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::Renew, Mage)}));
}

TEST(PriestHealerDelay, HealsAtOnceInAFightWithoutAPull)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetHealth(Tank, 400, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::Renew, Tank)}));
}

TEST(GroupCoordinator, NotesWhenTheTankEngages)
{
    GroupCoordinator group;
    group.StartPull(Tank, Kobold);
    group.HoldPull(Tank, 5000);
    group.HoldPull(Tank, 9000);

    EXPECT_EQ(group.GetPull().engagedMs, 5000);
}

// jvbastel/WOWAI#132: the shield absorbs the damage that gives a rage tank
// its rage, so the tank would lose threat. A rage tank near death gets the
// next heal of the strategy, never the shield.
TEST(PriestHealerRageTank, HealsARageTankNearDeathWithoutAShield)
{
    PriestHealer priest;
    priest.InFight();
    priest.fake.SetHealth(Tank, 150, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::Heal, Tank)}));
}

TEST(PriestHealerRageTank, ShieldsARogueNearDeath)
{
    Bots::Guid const Rogue = 0x0000000000000014;
    PriestHealer priest;
    priest.fake.SetGroupMembers({Tank, Mage, Rogue});
    priest.fake.SetHealth(Rogue, 100, 400);
    priest.fake.SetDistance(Rogue, 20.0f);
    priest.fake.SetAura(Rogue, Priest::PowerWordFortitude);
    priest.fake.SetPowerKind(Rogue, Bots::PowerKind::Energy);
    priest.InFight();

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::PowerWordShield, Rogue)}));
}

TEST(PriestHealerRageTank, ShieldsATankWithManaNearDeath)
{
    // The rule checks the power kind, not the class: a tank with mana
    // gives no rage away, and gets the shield.
    PriestHealer priest;
    priest.fake.SetPowerKind(Tank, Bots::PowerKind::Mana);
    priest.InFight();
    priest.fake.SetHealth(Tank, 150, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::PowerWordShield, Tank)}));
}

TEST(PriestHealerRageTank, ShieldsARageMemberThatIsNotTheTank)
{
    // A second warrior, in the damage role, has rage but holds no threat:
    // the shield does not cost the group its threat.
    PriestHealer priest;
    priest.fake.SetPowerKind(Mage, Bots::PowerKind::Rage);
    priest.InFight();
    priest.fake.SetHealth(Mage, 80, 300);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::PowerWordShield, Mage)}));
}

// jvbastel/WOWAI#134: Lesser Heal and Heal pick their rank by the missing
// health of the target. Renew keeps its highest rank.
namespace
{
    Bots::SpellId const LesserHeal2 = 2052;
    Bots::SpellId const LesserHeal3 = 2053;
    Bots::SpellId const Heal2 = 2055;
    Bots::SpellId const Renew2 = 6074;

    // The healer knows each rank of Lesser Heal and Heal, and Renew rank 2,
    // with the average heals and mana costs of 1.12, and has full mana.
    void KnowTheHealRanks(PriestHealer& priest)
    {
        FakeBot& fake = priest.fake;
        fake.SetSpellChain({Priest::LesserHeal, LesserHeal2, LesserHeal3});
        fake.SetSpellChain({Priest::Heal, Heal2});
        fake.SetSpellChain({Priest::Renew, Renew2});
        for (Bots::SpellId rank : {LesserHeal2, LesserHeal3, Heal2, Renew2})
            fake.SetKnownSpell(rank);
        fake.SetHealRank(Priest::LesserHeal, 53, 30);
        fake.SetHealRank(LesserHeal2, 84, 45);
        fake.SetHealRank(LesserHeal3, 154, 75);
        fake.SetHealRank(Priest::Heal, 330, 155);
        fake.SetHealRank(Heal2, 476, 205);
        fake.SetHealRank(Priest::Renew, 0, 30);
        fake.SetHealRank(Renew2, 0, 65);
        fake.SetMana(Healer, 400, 400);
    }
}

TEST(PriestHealerRank, ASmallWoundGetsALowRankOfLesserHeal)
{
    PriestHealer priest;
    KnowTheHealRanks(priest);
    priest.InFight();
    // 70 missing, under the Lesser Heal fraction, with Renew on already.
    priest.fake.SetHealth(Mage, 130, 200);
    priest.fake.SetAura(Mage, Priest::Renew);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(LesserHeal2, Mage)}));
}

TEST(PriestHealerRank, AWoundThatHealRankOneCoversGetsHealRankOne)
{
    PriestHealer priest;
    KnowTheHealRanks(priest);
    priest.InFight();
    priest.fake.SetHealth(Tank, 290, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::Heal, Tank)}));
}

TEST(PriestHealerRank, ABigWoundGetsTheHighestRank)
{
    PriestHealer priest;
    KnowTheHealRanks(priest);
    priest.InFight();
    priest.fake.SetHealth(Tank, 200, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Heal2, Tank)}));
}

TEST(PriestHealerRank, ALowLevelTargetKeepsARankThatItsLevelAllows)
{
    PriestHealer priest;
    KnowTheHealRanks(priest);
    priest.InFight();
    priest.fake.SetHealth(Tank, 200, 600);
    priest.fake.SetLevelTooLowFor(Heal2, Tank);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::Heal, Tank)}));
}

TEST(PriestHealerRank, RenewKeepsItsHighestRankOnASmallWound)
{
    PriestHealer priest;
    KnowTheHealRanks(priest);
    priest.InFight();
    priest.fake.SetHealth(Tank, 500, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Renew2, Tank)}));
}

// jvbastel/WOWAI#136: from level 40 the healer knows Greater Heal. It is the
// heal of the badly hurt band, before Heal: it heals the most per mana.
namespace
{
    Bots::SpellId const GreaterHeal2 = 10963;

    // The healer knows both ranks of Greater Heal, with the average heals and
    // mana costs of 1.12, and the ranks of Lesser Heal and Heal. It has 2000
    // mana, enough for every rank.
    void KnowGreaterHeal(PriestHealer& priest)
    {
        KnowTheHealRanks(priest);
        FakeBot& fake = priest.fake;
        fake.SetSpellChain({Priest::GreaterHeal, GreaterHeal2});
        fake.SetKnownSpell(Priest::GreaterHeal);
        fake.SetKnownSpell(GreaterHeal2);
        fake.SetHealRank(Priest::GreaterHeal, 982, 370);
        fake.SetHealRank(GreaterHeal2, 1270, 455);
        fake.SetMana(Healer, 2000, 2000);
    }
}

TEST(PriestHealerGreaterHeal, NamesGreaterHealByTheFirstRank)
{
    std::ifstream file(std::string(BOT_TEST_DATA_DIR) + "/learnable-spells.tsv", std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    EXPECT_EQ(Priest::GreaterHeal, 2060u);
    EXPECT_NE(text.str().find("\n2060\tGreater Heal\tRank 1\n"), std::string::npos);
}

TEST(PriestHealerGreaterHeal, HealsABadlyHurtMemberWithGreaterHeal)
{
    PriestHealer priest;
    KnowGreaterHeal(priest);
    priest.InFight();
    // 950 missing: rank 1 (982) covers it.
    priest.fake.SetHealth(Tank, 850, 1800);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::GreaterHeal, Tank)}));
}

TEST(PriestHealerGreaterHeal, ABigWoundGetsTheHighestRankOfGreaterHeal)
{
    PriestHealer priest;
    KnowGreaterHeal(priest);
    priest.InFight();
    // 1400 missing: no rank covers it.
    priest.fake.SetHealth(Tank, 1100, 2500);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(GreaterHeal2, Tank)}));
}

TEST(PriestHealerGreaterHeal, ABotThatDoesNotKnowGreaterHealKeepsHeal)
{
    PriestHealer priest;
    KnowTheHealRanks(priest);
    priest.InFight();
    // 400 missing: Heal rank 2 (476) covers it.
    priest.fake.SetHealth(Tank, 200, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Heal2, Tank)}));
}

TEST(PriestHealerGreaterHeal, AHealerThatCannotPayForGreaterHealCastsHeal)
{
    PriestHealer priest;
    KnowGreaterHeal(priest);
    priest.InFight();
    // 300 mana: under the 370 of Greater Heal rank 1, over the 205 of Heal rank 2.
    priest.fake.SetMana(Healer, 300, 2000);
    priest.fake.SetHealth(Tank, 200, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Heal2, Tank)}));
}

TEST(PriestHealerGreaterHeal, ATargetTooLowForGreaterHealGetsHeal)
{
    PriestHealer priest;
    KnowGreaterHeal(priest);
    priest.InFight();
    priest.fake.SetHealth(Tank, 200, 600);
    priest.fake.SetLevelTooLowFor(Priest::GreaterHeal, Tank);
    priest.fake.SetLevelTooLowFor(GreaterHeal2, Tank);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Heal2, Tank)}));
}

TEST(PriestHealerGreaterHeal, ALightlyHurtMemberDoesNotGetGreaterHeal)
{
    PriestHealer priest;
    KnowGreaterHeal(priest);
    priest.InFight();
    // Above the badly hurt band, with Renew on already: Lesser Heal.
    priest.fake.SetHealth(Mage, 130, 200);
    priest.fake.SetAura(Mage, Priest::Renew);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(LesserHeal2, Mage)}));
}

// jvbastel/WOWAI#135: from level 20 the healer knows Flash Heal. It casts it
// only for a member near death, who dies before a slow heal lands. In every
// other case it casts its slow heals, which give more health per mana.
namespace
{
    Bots::SpellId const FlashHeal2 = 9472;

    // A level 20 healer: it knows Flash Heal, and the heal ranks of #134.
    void KnowFlashHeal(PriestHealer& priest)
    {
        KnowTheHealRanks(priest);
        priest.fake.SetKnownSpell(Priest::FlashHeal);
        priest.fake.SetHealRank(Priest::FlashHeal, 224, 125);
    }

    // A level 26 healer: it knows Flash Heal rank 2 too.
    void KnowFlashHealRank2(PriestHealer& priest)
    {
        KnowFlashHeal(priest);
        priest.fake.SetSpellChain({Priest::FlashHeal, FlashHeal2});
        priest.fake.SetKnownSpell(FlashHeal2);
        priest.fake.SetHealRank(FlashHeal2, 297, 155);
    }
}

TEST(PriestHealerFlashHeal, FlashHealsARageTankNearDeath)
{
    PriestHealer priest;
    KnowFlashHeal(priest);
    priest.InFight();
    priest.fake.SetHealth(Tank, 150, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::FlashHeal, Tank)}));
}

TEST(PriestHealerFlashHeal, NamesFlashHealByItsFirstRank)
{
    // PriestHealer.NamesItsSpellsByTheFirstRank finds 2061 in the data.
    EXPECT_EQ(Priest::FlashHeal, 2061u);
}

TEST(PriestHealerFlashHeal, ABigWoundNearDeathGetsTheHighestRank)
{
    PriestHealer priest;
    KnowFlashHealRank2(priest);
    priest.InFight();
    // 450 missing: no rank covers it.
    priest.fake.SetHealth(Tank, 150, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(FlashHeal2, Tank)}));
}

TEST(PriestHealerFlashHeal, ASmallerWoundNearDeathGetsTheRankThatCoversIt)
{
    PriestHealer priest;
    KnowFlashHealRank2(priest);
    priest.InFight();
    // 220 missing: rank 1 (224) covers it.
    priest.fake.SetHealth(Tank, 80, 300);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::FlashHeal, Tank)}));
}

TEST(PriestHealerFlashHeal, AMemberAboveTheFractionGetsASlowHeal)
{
    PriestHealer priest;
    KnowFlashHeal(priest);
    priest.InFight();
    // 33% health: badly hurt, not near death.
    priest.fake.SetHealth(Tank, 200, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Heal2, Tank)}));
}

TEST(PriestHealerFlashHeal, AMemberAtExactlyTheFractionGetsASlowHeal)
{
    PriestHealer priest;
    KnowFlashHeal(priest);
    priest.InFight();
    // 30% health: the band is "below" the fraction.
    priest.fake.SetHealth(Tank, 180, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Heal2, Tank)}));
}

TEST(PriestHealerFlashHeal, ALowManaBotNearDeathDropsToALesserHeal)
{
    PriestHealer priest;
    KnowFlashHeal(priest);
    priest.InFight();
    priest.fake.SetHealth(Tank, 150, 600);
    // 100 mana: no rank of Flash Heal (125) or Heal (155) fits.
    priest.fake.SetMana(Healer, 100, 400);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(LesserHeal3, Tank)}));
}

TEST(PriestHealerFlashHeal, ABotThatDoesNotKnowFlashHealKeepsItsHeals)
{
    PriestHealer priest;
    KnowTheHealRanks(priest);
    priest.InFight();
    priest.fake.SetHealth(Tank, 150, 600);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Heal2, Tank)}));
}

TEST(PriestHealerFlashHeal, AMemberNearDeathGetsTheShieldFirst)
{
    PriestHealer priest;
    KnowFlashHeal(priest);
    priest.InFight();
    priest.fake.SetHealth(Mage, 80, 300);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::PowerWordShield, Mage)}));
}

TEST(PriestHealerFlashHeal, AShieldedMemberStillNearDeathGetsFlashHeal)
{
    PriestHealer priest;
    KnowFlashHeal(priest);
    priest.InFight();
    priest.fake.SetHealth(Mage, 80, 300);
    priest.fake.SetAura(Mage, Priest::PowerWordShield);
    priest.fake.SetAura(Mage, Priest::WeakenedSoul);

    priest.Think();

    EXPECT_EQ(priest.fake.Deeds(), (std::vector<std::string>{CastOn(Priest::FlashHeal, Mage)}));
}

// Spec #131, story 24: Holy Nova pulls the enemies near the healer and heals
// little per mana. No rule casts it, even when the bot knows it, the whole
// group is hurt, and no other spell can be cast.
namespace
{
    enum class Situation
    {
        OutOfFight,
        InFight,
        HealerIsTheVictim,
    };
}

TEST(PriestHealerHolyNova, NeverCastsHolyNova)
{
    // Rank 1 is the talent (not in the learnable spells), rank 2 is 15430.
    Bots::SpellId const HolyNova = 15237;
    Bots::SpellId const HolyNova2 = 15430;
    for (std::uint32_t current : {30u, 100u, 200u, 400u, 550u, 600u})
        for (Situation situation : {Situation::OutOfFight, Situation::InFight, Situation::HealerIsTheVictim})
            for (bool othersBlocked : {false, true})
            {
                PriestHealer priest;
                KnowFlashHeal(priest);
                priest.fake.SetSpellChain({HolyNova, HolyNova2});
                priest.fake.SetKnownSpell(HolyNova);
                priest.fake.SetKnownSpell(HolyNova2);
                if (situation != Situation::OutOfFight)
                    priest.InFight();
                if (situation == Situation::HealerIsTheVictim)
                    priest.fake.SetVictim(Kobold, Healer);
                for (Bots::Guid member : {Healer, Tank, Mage})
                    priest.fake.SetHealth(member, current, 600);
                if (othersBlocked)
                    for (Bots::SpellId spell : {Priest::LesserHeal, LesserHeal2, LesserHeal3, Priest::Heal, Heal2,
                             Priest::FlashHeal, Priest::Renew, Renew2, Priest::PowerWordShield,
                             Priest::PowerWordFortitude, Priest::InnerFire, Priest::Fade})
                        priest.fake.SetCannotCast(spell);

                priest.Think();

                for (std::string const& deed : priest.fake.Deeds())
                    for (Bots::SpellId nova : {HolyNova, HolyNova2})
                    {
                        std::string const castNova = "cast " + std::to_string(nova) + " ";
                        EXPECT_NE(deed.compare(0, castNova.size(), castNova), 0)
                            << deed << " at " << current << " health, situation "
                            << static_cast<int>(situation);
                    }
            }
}
