#include "ClassModules.h"
#include "MageModule.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

#include <cstdio>

using Bots::BotContext;
using Bots::GroupCoordinator;
using Bots::NoGuid;
using Bots::StrategyStack;
using Bots::Tests::FakeBot;
namespace Mage = Bots::Mage;

namespace
{
    Bots::Guid const MageBot = 0x0000000000000021;
    Bots::Guid const Tank = 0x0000000000000011;
    Bots::Guid const Kobold = 0xF130000302000022;
    Bots::Guid const Healer = 0x0000000000000031;
    Bots::Guid const Warlock = 0x0000000000000041; // A ranged member.
    Bots::Guid const Rogue = 0x0000000000000051;   // A melee member.
    Bots::Guid const Runner = 0xF130000302000066;  // A loose enemy.

    std::string CastOn(Bots::SpellId spell, Bots::Guid target)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "cast %u on 0x%016llx", spell, static_cast<unsigned long long>(target));
        return text;
    }

    // A level 20 frost Mage 20 yards from a kobold that the tank pulls.
    struct FrostMage
    {
        FakeBot fake{MageBot};
        GroupCoordinator group;
        StrategyStack stack = Bots::MakeStrategyStack(Bots::BotClass::Mage, Bots::BotRole::Ranged);

        // A Mage below level 4 knows Fireball, but no Frostbolt yet.
        explicit FrostMage(bool knowsFrostbolt = true)
        {
            fake.SetKnownSpell(Mage::Fireball);
            if (knowsFrostbolt)
                fake.SetKnownSpell(Mage::Frostbolt);
            fake.SetHealth(MageBot, 400, 400);
            fake.SetPower(MageBot, 600, 600);
            fake.SetHealth(Kobold, 200, 200);
            fake.SetDistance(Kobold, 20.0f);
        }

        std::string Think(Bots::CommandState command = Bots::CommandState::None, Bots::Guid player = NoGuid)
        {
            BotContext const context{fake, fake, group, command, player};
            return stack.Think(context);
        }

        // A think at `nowMs` on the clock (BotContext::nowMs).
        std::string ThinkAt(std::int64_t nowMs)
        {
            BotContext context{fake, fake, group, Bots::CommandState::None, NoGuid};
            context.nowMs = nowMs;
            return stack.Think(context);
        }

        // jvbastel/WOWAI#143: the group of the peel nova. The tank holds
        // the kobold; the healer, a ranged member and a melee member stand
        // by. Each bot set its role (GroupCoordinator::SetMemberRole).
        void PeelGroup(bool knowsFrostNova = true)
        {
            if (knowsFrostNova)
                fake.SetKnownSpell(Mage::FrostNova);
            TankHoldsKobold();
            fake.SetInCombat(MageBot);
            fake.SetGroupMembers({Tank, Healer, Warlock, Rogue});
            for (Bots::Guid member : {Tank, Healer, Warlock, Rogue})
                fake.SetHealth(member, 500, 500);
            group.SetTank(Tank);
            group.SetHealer(Healer);
            group.SetMemberRole(Tank, Bots::BotRole::Tank);
            group.SetMemberRole(Healer, Bots::BotRole::Healer);
            group.SetMemberRole(Warlock, Bots::BotRole::Ranged);
            group.SetMemberRole(Rogue, Bots::BotRole::Melee);
            group.SetMemberRole(MageBot, Bots::BotRole::Ranged);
        }

        // Loose enemy `enemy` hits member `victim` in melee, `yards` from
        // the Mage.
        void Hits(Bots::Guid enemy, Bots::Guid victim, float yards)
        {
            fake.SetHealth(enemy, 300, 300);
            fake.SetVictim(enemy, victim);
            fake.SetDistance(enemy, yards);
            fake.SetDistanceBetween(enemy, victim, 2.0f);
            fake.SetDistanceBetween(victim, enemy, 2.0f);
        }

        // The tank pulled the kobold, and the kobold attacks the tank.
        void TankHoldsKobold()
        {
            group.StartPull(Tank, Kobold);
            Bots::Tests::HoldThreat(group, Tank);
            fake.SetVictim(Kobold, Tank);
        }
    };
}

TEST(MageFrost, CastsFrostboltOnTheFocusTargetWhenTheTankHoldsThreat)
{
    FrostMage mage;
    mage.TankHoldsKobold();

    mage.Think();

    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{CastOn(Mage::Frostbolt, Kobold)}));
}

TEST(MageFrost, WaitsWhileTheTankPulls)
{
    FrostMage mage;
    mage.group.StartPull(Tank, Kobold);

    EXPECT_EQ(mage.Think(), "");
    EXPECT_TRUE(mage.fake.Deeds().empty());
}

TEST(MageFrost, NeverPicksATargetOfItsOwn)
{
    FrostMage mage;
    mage.fake.SetVictim(Kobold, Tank);

    EXPECT_EQ(mage.Think(), "");
}

TEST(MageFrost, AttacksTheFocusTargetOutsideAPull)
{
    // The tank fights back, or the player names the target with `attack`.
    FrostMage mage;
    mage.group.SetFocusTarget(Kobold);

    mage.Think();

    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{CastOn(Mage::Frostbolt, Kobold)}));
}

TEST(MageFrost, CastsTheHighestKnownRankOfFrostbolt)
{
    FrostMage mage;
    mage.fake.SetSpellChain({Mage::Frostbolt, 205, 837, 7322});
    mage.fake.SetKnownSpell(205);
    mage.fake.SetKnownSpell(837);
    mage.TankHoldsKobold();

    mage.Think();

    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{CastOn(837, Kobold)}));
}

TEST(MageFrost, CastsFireballBeforeItLearnsFrostbolt)
{
    FrostMage mage(false);
    mage.TankHoldsKobold();

    mage.Think();

    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{CastOn(Mage::Fireball, Kobold)}));
}

TEST(MageFrost, RunsIntoCastingRangeOfAFarFocusTarget)
{
    FrostMage mage;
    mage.fake.SetDistance(Kobold, 40.0f);
    mage.TankHoldsKobold();

    mage.Think();

    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000022 to 25 yards"}));
}

TEST(MageFrost, RunsIntoFrostboltRangeRatherThanCastFireballAtTheEdge)
{
    // Spell ranges count from the combat reach, which is smaller here than
    // the body: Frostbolt (30 yards) is out of range, Fireball (35) not.
    FrostMage mage;
    mage.fake.SetSpellRange(Mage::Frostbolt, 30.0f);
    mage.fake.SetSpellRange(Mage::Fireball, 35.0f);
    mage.fake.SetDistance(Kobold, 29.0f);
    mage.fake.SetSpellDistance(Kobold, 32.0f);
    mage.TankHoldsKobold();

    mage.Think();

    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000022 to 25 yards"}));
}

TEST(MageFrost, FrostNovasAnEnemyThatHitsItInMelee)
{
    FrostMage mage;
    mage.fake.SetKnownSpell(Mage::FrostNova);
    mage.TankHoldsKobold();
    mage.fake.SetVictim(Kobold, MageBot);
    mage.fake.SetDistance(Kobold, 3.0f);

    mage.Think();

    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{CastOn(Mage::FrostNova, MageBot)}));
}

// jvbastel/WOWAI#102 review: the shared IsHitInMelee (CommonRules.h) skips a
// dead attacker.
TEST(MageFrost, NoFrostNovaForADeadEnemy)
{
    FrostMage mage;
    mage.fake.SetKnownSpell(Mage::FrostNova);
    mage.TankHoldsKobold();
    mage.fake.SetVictim(Kobold, MageBot);
    mage.fake.SetDistance(Kobold, 3.0f);
    mage.fake.SetHealth(Kobold, 0, 200);

    mage.Think();

    for (std::string const& deed : mage.fake.Deeds())
        EXPECT_NE(deed, CastOn(Mage::FrostNova, MageBot));
}

TEST(MageFrost, ShieldsItselfWithIceBarrierWhenAttacked)
{
    FrostMage mage;
    mage.fake.SetKnownSpell(Mage::IceBarrier);
    mage.TankHoldsKobold();
    mage.fake.SetVictim(Kobold, MageBot);

    mage.Think();

    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{CastOn(Mage::IceBarrier, MageBot)}));
}

TEST(MageFrost, EvocatesWithLittleManaInTheFight)
{
    FrostMage mage;
    mage.fake.SetKnownSpell(Mage::Evocation);
    mage.TankHoldsKobold();
    mage.fake.SetInCombat(MageBot);
    mage.fake.SetPower(MageBot, 40, 600);

    mage.Think();

    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{CastOn(Mage::Evocation, MageBot)}));
}

TEST(MageFrost, FinishesALowFocusTargetWithFireBlast)
{
    FrostMage mage;
    mage.fake.SetKnownSpell(Mage::FireBlast);
    mage.TankHoldsKobold();
    mage.fake.SetHealth(Kobold, 30, 200);

    mage.Think();

    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{CastOn(Mage::FireBlast, Kobold)}));
}

TEST(MageFrost, PutsOnFrostArmorOutOfCombat)
{
    FrostMage mage;
    mage.fake.SetKnownSpell(Mage::FrostArmor);

    mage.Think();

    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{CastOn(Mage::FrostArmor, MageBot)}));
}

TEST(MageFrost, PrefersIceArmorWhenItKnowsIt)
{
    FrostMage mage;
    mage.fake.SetKnownSpell(Mage::FrostArmor);
    mage.fake.SetKnownSpell(Mage::IceArmor);

    mage.Think();

    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{CastOn(Mage::IceArmor, MageBot)}));
}

TEST(MageFrost, CastsArcaneIntellectOnItselfOutOfCombat)
{
    FrostMage mage;
    mage.fake.SetKnownSpell(Mage::FrostArmor);
    mage.fake.SetAura(MageBot, Mage::FrostArmor);
    mage.fake.SetKnownSpell(Mage::ArcaneIntellect);

    mage.Think();

    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{CastOn(Mage::ArcaneIntellect, MageBot)}));
}

TEST(MageFrost, DoesNotBuffInTheFight)
{
    FrostMage mage;
    mage.fake.SetKnownSpell(Mage::FrostArmor);
    mage.fake.SetKnownSpell(Mage::ArcaneIntellect);
    mage.TankHoldsKobold();
    mage.fake.SetInCombat(MageBot);

    mage.Think();

    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{CastOn(Mage::Frostbolt, Kobold)}));
}

// jvbastel/WOWAI#143: the peel nova.

namespace
{
    // The Mage cast no Frost Nova and made no run for one.
    void ExpectNoNovaAndNoMove(std::vector<std::string> const& deeds)
    {
        for (std::string const& deed : deeds)
        {
            EXPECT_NE(deed, CastOn(Mage::FrostNova, MageBot));
            EXPECT_EQ(deed.find("chase"), std::string::npos) << deed;
        }
    }
}

TEST(MagePeelNova, NovasAnEnemyOnTheHealerAtOnce)
{
    FrostMage mage;
    mage.PeelGroup();
    mage.Hits(Runner, Healer, 6.0f);

    EXPECT_EQ(mage.ThinkAt(1000), "peel nova");
    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{CastOn(Mage::FrostNova, MageBot)}));
}

TEST(MagePeelNova, WaitsForTheTanksTauntBeforeItNovasAnEnemyOnARangedMember)
{
    FrostMage mage;
    mage.PeelGroup();
    mage.Hits(Runner, Warlock, 6.0f);

    EXPECT_NE(mage.ThinkAt(1000), "peel nova");
    EXPECT_NE(mage.ThinkAt(2400), "peel nova");
    mage.fake.ClearDeeds();
    EXPECT_EQ(mage.ThinkAt(2600), "peel nova");
    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{CastOn(Mage::FrostNova, MageBot)}));
}

TEST(MagePeelNova, NoNovaWhenTheTankTauntedTheEnemyOffTheRangedMember)
{
    FrostMage mage;
    mage.PeelGroup();
    mage.Hits(Runner, Warlock, 6.0f);
    mage.ThinkAt(1000);
    mage.fake.SetVictim(Runner, Tank);

    EXPECT_NE(mage.ThinkAt(2600), "peel nova");
}

TEST(MagePeelNova, AnEnemyBackOnARangedMemberWaitsAgain)
{
    FrostMage mage;
    mage.PeelGroup();
    mage.Hits(Runner, Warlock, 6.0f);
    mage.ThinkAt(1000);
    mage.fake.SetVictim(Runner, Tank);
    mage.ThinkAt(2000);
    mage.fake.SetVictim(Runner, Warlock);

    EXPECT_NE(mage.ThinkAt(5000), "peel nova");
    EXPECT_NE(mage.ThinkAt(6000), "peel nova");
    EXPECT_EQ(mage.ThinkAt(6600), "peel nova");
}

TEST(MagePeelNova, NovasAnEnemyThatHitsItAtOnce)
{
    FrostMage mage;
    mage.PeelGroup();
    mage.Hits(Runner, MageBot, 3.0f);

    EXPECT_EQ(mage.ThinkAt(1000), "peel nova");
    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{CastOn(Mage::FrostNova, MageBot)}));
}

TEST(MagePeelNova, ClosesInToNovaRangeOfAnEnemyWithin15Yards)
{
    FrostMage mage;
    mage.PeelGroup();
    mage.Hits(Runner, Healer, 13.0f);

    EXPECT_EQ(mage.ThinkAt(1000), "peel nova");
    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000066 to 8 yards"}));
}

TEST(MagePeelNova, DoesNothingNewForAnEnemyFartherThan15Yards)
{
    FrostMage mage;
    mage.PeelGroup();
    mage.Hits(Runner, Healer, 20.0f);

    mage.ThinkAt(1000);

    // Today: the Frostbolt of the damage rules, on its own pick.
    EXPECT_FALSE(mage.fake.Deeds().empty());
    ExpectNoNovaAndNoMove(mage.fake.Deeds());
}

TEST(MagePeelNova, TheHealerComesFirst)
{
    // The enemy on the ranged member waited long enough and stands in nova
    // range; the one on the healer needs a run. The Mage goes for the healer.
    Bots::Guid const Other = 0xF130000302000077;
    FrostMage mage;
    mage.PeelGroup();
    mage.Hits(Other, Warlock, 6.0f);
    mage.ThinkAt(1000);
    mage.Hits(Runner, Healer, 13.0f);
    mage.fake.ClearDeeds();

    EXPECT_EQ(mage.ThinkAt(3000), "peel nova");
    EXPECT_EQ(mage.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000066 to 8 yards"}));
}

TEST(MagePeelNova, NoNovaForAnEnemyOnTheTank)
{
    FrostMage mage;
    mage.PeelGroup();
    mage.Hits(Runner, Tank, 6.0f);

    mage.ThinkAt(1000);
    mage.ThinkAt(5000);

    for (std::string const& deed : mage.fake.Deeds())
        EXPECT_NE(deed, CastOn(Mage::FrostNova, MageBot));
}

TEST(MagePeelNova, NoNovaForAnEnemyOnAMeleeMember)
{
    FrostMage mage;
    mage.PeelGroup();
    mage.Hits(Runner, Rogue, 6.0f);

    mage.ThinkAt(1000);
    mage.ThinkAt(5000);

    for (std::string const& deed : mage.fake.Deeds())
        EXPECT_NE(deed, CastOn(Mage::FrostNova, MageBot));
}

TEST(MagePeelNova, NoNovaForAnEnemyThatAttacksTheHealerFromRange)
{
    FrostMage mage;
    mage.PeelGroup();
    mage.Hits(Runner, Healer, 6.0f);
    mage.fake.SetDistanceBetween(Runner, Healer, 25.0f);

    EXPECT_NE(mage.ThinkAt(1000), "peel nova");
}

TEST(MagePeelNova, NoNovaForAnEnemyThatIsFrozenAlready)
{
    FrostMage mage;
    mage.PeelGroup();
    mage.Hits(Runner, Healer, 6.0f);
    mage.fake.SetAura(Runner, Mage::FrostNova);

    EXPECT_NE(mage.ThinkAt(1000), "peel nova");
}

TEST(MagePeelNova, OnCooldownTheMageDoesAsTodayAndDoesNotMove)
{
    FrostMage mage;
    mage.PeelGroup();
    mage.fake.SetCannotCast(Mage::FrostNova);
    mage.Hits(Runner, Healer, 13.0f);

    mage.ThinkAt(1000);

    // Today: the Frostbolt of the damage rules, on its own pick.
    EXPECT_FALSE(mage.fake.Deeds().empty());
    ExpectNoNovaAndNoMove(mage.fake.Deeds());
}

TEST(MagePeelNova, WithoutFrostNovaTheMageDoesAsTodayAndDoesNotMove)
{
    FrostMage mage;
    mage.PeelGroup(false);
    mage.Hits(Runner, Healer, 13.0f);

    mage.ThinkAt(1000);

    // Today: the Frostbolt of the damage rules, on its own pick.
    EXPECT_FALSE(mage.fake.Deeds().empty());
    ExpectNoNovaAndNoMove(mage.fake.Deeds());
}
