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
