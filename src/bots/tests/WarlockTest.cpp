#include "ClassModules.h"
#include "WarlockModule.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

using Bots::BotContext;
using Bots::GroupCoordinator;
using Bots::NoGuid;
using Bots::StrategyStack;
using Bots::Tests::FakeBot;
namespace Warlock = Bots::Warlock;

namespace
{
    Bots::Guid const Tank = 0x0000000000000011;
    Bots::Guid const Self = 0x0000000000000014;
    Bots::Guid const Player = 0x0000000000000001;
    Bots::Guid const Imp = 0xF140000001000044;
    Bots::Guid const Kobold = 0xF130000302000022;
    Bots::Guid const Wolf = 0xF130000302000033;

    std::string CastOn(Bots::SpellId spell, Bots::Guid target)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "cast %u on 0x%016llx", spell, static_cast<unsigned long long>(target));
        return text;
    }

    // A level 16 Warlock with its Imp and Demon Skin, that follows the
    // player, in a group with a tank.
    struct WarlockBot
    {
        FakeBot fake{Self};
        GroupCoordinator group;
        StrategyStack stack = Bots::MakeStrategyStack(Bots::BotClass::Warlock, Bots::BotRole::Ranged);
        Bots::Guid attackTarget = NoGuid;

        WarlockBot()
        {
            for (Bots::SpellId spell : {Warlock::SummonImp, Warlock::DemonSkin, Warlock::ShadowBolt, Warlock::Immolate,
                     Warlock::Corruption, Warlock::CurseOfAgony, Warlock::LifeTap})
                fake.SetKnownSpell(spell);
            fake.SetHealth(Self, 400, 400);
            fake.SetPower(Self, 500, 500);
            fake.SetHealth(Player, 400, 400);
            fake.SetFollowTarget(Player);
            fake.SetHealth(Imp, 200, 200);
            fake.SetPet(Imp);
            fake.SetAura(Self, Warlock::DemonSkin);
            fake.SetHealth(Kobold, 200, 200);
            fake.SetHealth(Wolf, 200, 200);
        }

        std::string Think()
        {
            BotContext const context{fake, fake, group, Bots::CommandState::Follow, Player, attackTarget};
            return stack.Think(context);
        }

        // The tank holds the kobold. The Warlock stands in range of it and
        // the Imp attacks it.
        void InFightWithKobold()
        {
            group.StartPull(Tank, Kobold);
            fake.SetVictim(Kobold, Tank);
            group.HoldPull(Tank);
            fake.SetInCombat(Self);
            fake.SetVictim(Imp, Kobold);
            fake.ChaseInRange(Kobold, 20.0f);
            fake.ClearDeeds();
        }
    };
}

TEST(Warlock, SummonsTheImpOutOfCombat)
{
    WarlockBot warlock;
    warlock.fake.SetPet(NoGuid);

    warlock.Think();

    EXPECT_EQ(warlock.fake.Deeds(), (std::vector<std::string>{CastOn(Warlock::SummonImp, Self)}));
}

TEST(Warlock, PutsOnDemonSkinOutOfCombat)
{
    WarlockBot warlock;
    warlock.fake.SetAura(Self, Warlock::DemonSkin, false);

    warlock.Think();

    EXPECT_EQ(warlock.fake.Deeds(), (std::vector<std::string>{CastOn(Warlock::DemonSkin, Self)}));
}

TEST(Warlock, PrefersDemonArmorWhenItKnowsIt)
{
    WarlockBot warlock;
    warlock.fake.SetKnownSpell(Warlock::DemonArmor);

    warlock.Think();

    EXPECT_EQ(warlock.fake.Deeds(), (std::vector<std::string>{CastOn(Warlock::DemonArmor, Self)}));
}

TEST(Warlock, WaitsWhileTheTankPulls)
{
    WarlockBot warlock;
    warlock.group.StartPull(Tank, Kobold);

    warlock.Think();

    EXPECT_TRUE(warlock.fake.Deeds().empty());
}

TEST(Warlock, SendsTheImpOnceTheTankHoldsThreat)
{
    WarlockBot warlock;
    warlock.group.StartPull(Tank, Kobold);
    warlock.fake.SetVictim(Kobold, Tank);
    warlock.group.HoldPull(Tank);

    warlock.Think();

    EXPECT_EQ(warlock.fake.Deeds(), (std::vector<std::string>{"pet attack 0xf130000302000022"}));
}

TEST(Warlock, GetsInRangeOfTheTarget)
{
    WarlockBot warlock;
    warlock.group.StartPull(Tank, Kobold);
    warlock.fake.SetVictim(Kobold, Tank);
    warlock.group.HoldPull(Tank);
    warlock.fake.SetVictim(Imp, Kobold);

    warlock.Think();

    EXPECT_EQ(warlock.fake.Deeds(), (std::vector<std::string>{"chase in range 0xf130000302000022"}));
}

TEST(Warlock, PutsItsDamageOverTimeSpellsOnTheTarget)
{
    WarlockBot warlock;
    warlock.InFightWithKobold();

    warlock.Think();
    warlock.fake.SetAura(Kobold, Warlock::Corruption);
    warlock.Think();
    warlock.fake.SetAura(Kobold, Warlock::CurseOfAgony);
    warlock.Think();
    warlock.fake.SetAura(Kobold, Warlock::Immolate);
    warlock.Think();

    EXPECT_EQ(warlock.fake.Deeds(), (std::vector<std::string>{CastOn(Warlock::Corruption, Kobold),
                                         CastOn(Warlock::CurseOfAgony, Kobold), CastOn(Warlock::Immolate, Kobold),
                                         CastOn(Warlock::ShadowBolt, Kobold)}));
}

TEST(Warlock, LifeTapsWithLittleManaAndMuchHealth)
{
    WarlockBot warlock;
    warlock.InFightWithKobold();
    warlock.fake.SetPower(Self, 50, 500);

    warlock.Think();

    EXPECT_EQ(warlock.fake.Deeds(), (std::vector<std::string>{CastOn(Warlock::LifeTap, Self)}));
}

TEST(Warlock, SwitchesItAndTheImpToTheAttackTargetOfThePlayer)
{
    WarlockBot warlock;
    warlock.InFightWithKobold();
    warlock.attackTarget = Wolf;

    warlock.Think();
    warlock.fake.SetVictim(Imp, Wolf);
    warlock.Think();

    EXPECT_EQ(warlock.fake.Deeds(),
        (std::vector<std::string>{"pet attack 0xf130000302000033", "chase in range 0xf130000302000033"}));
}

TEST(Warlock, DoesNotFollowWhileItChasesALivingTarget)
{
    WarlockBot warlock;
    warlock.InFightWithKobold();
    warlock.fake.SetInCombat(Self, false);
    warlock.fake.SetFollowTarget(NoGuid);
    for (Bots::SpellId spell : {Warlock::ShadowBolt, Warlock::Immolate, Warlock::Corruption, Warlock::CurseOfAgony,
             Warlock::LifeTap})
        warlock.fake.SetCannotCast(spell);

    warlock.Think();

    EXPECT_TRUE(warlock.fake.Deeds().empty());
}
