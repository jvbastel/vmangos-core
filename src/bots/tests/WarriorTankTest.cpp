#include "ClassModules.h"
#include "WarriorModule.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

using Bots::BotContext;
using Bots::GroupCoordinator;
using Bots::NoGuid;
using Bots::PullPhase;
using Bots::StrategyStack;
using Bots::Tests::FakeBot;
namespace Warrior = Bots::Warrior;

namespace
{
    Bots::Guid const Tank = 0x0000000000000011;
    Bots::Guid const Healer = 0x0000000000000012;
    Bots::Guid const Kobold = 0xF130000302000022;
    Bots::Guid const Wolf = 0xF130000302000033;

    std::string CastOn(Bots::SpellId spell, Bots::Guid target)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "cast %u on 0x%016llx", spell, static_cast<unsigned long long>(target));
        return text;
    }

    // A level 16 Warrior tank in Battle Stance, next to a kobold.
    struct WarriorTank
    {
        FakeBot fake{Tank};
        GroupCoordinator group;
        StrategyStack stack = Bots::MakeStrategyStack(Bots::BotClass::Warrior, Bots::BotRole::Tank);

        WarriorTank()
        {
            for (Bots::SpellId spell : {Warrior::BattleStance, Warrior::DefensiveStance, Warrior::Charge, Warrior::Taunt,
                     Warrior::HeroicStrike, Warrior::SunderArmor, Warrior::Revenge, Warrior::BattleShout, Warrior::Bloodrage})
                fake.SetKnownSpell(spell);
            fake.SetHealth(Tank, 600, 600);
            fake.SetPower(Tank, 0, 100);
            fake.SetHealth(Kobold, 200, 200);
            fake.SetDistance(Kobold, 20.0f);
            fake.SetAura(Tank, Warrior::BattleStance);
        }

        std::string Think(Bots::CommandState command = Bots::CommandState::None, Bots::Guid player = NoGuid)
        {
            BotContext const context{fake, fake, group, command, player};
            return stack.Think(context);
        }

        // The tank charged the kobold, and the kobold attacks it.
        void InFightWithKobold()
        {
            group.StartPull(Tank, Kobold);
            fake.SetVictim(Tank, Kobold);
            fake.SetVictim(Kobold, Tank);
            group.HoldPull(Tank);
            fake.SetAura(Tank, Warrior::BattleStance, false);
            fake.SetAura(Tank, Warrior::DefensiveStance);
            fake.SetAura(Tank, Warrior::BattleShout);
        }
    };
}

TEST(WarriorTank, NeverPullsOnItsOwn)
{
    WarriorTank tank;

    EXPECT_EQ(tank.Think(), "");
    EXPECT_TRUE(tank.fake.Deeds().empty());
}

TEST(WarriorTank, ChargesThePullTarget)
{
    WarriorTank tank;
    tank.group.StartPull(Tank, Kobold);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Charge, Kobold)}));
}

TEST(WarriorTank, TakesBattleStanceToCharge)
{
    WarriorTank tank;
    tank.fake.SetAura(Tank, Warrior::BattleStance, false);
    tank.fake.SetAura(Tank, Warrior::DefensiveStance);
    tank.group.StartPull(Tank, Kobold);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::BattleStance, Tank)}));
}

TEST(WarriorTank, RunsToThePullTargetWhenItCannotCharge)
{
    WarriorTank tank;
    tank.fake.SetCannotCast(Warrior::Charge);
    tank.group.StartPull(Tank, Kobold);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000022", "chase 0xf130000302000022"}));
}

TEST(WarriorTank, OnlyThePullerPulls)
{
    WarriorTank tank;
    tank.group.StartPull(Healer, Kobold);

    tank.Think();

    EXPECT_TRUE(tank.fake.Deeds().empty());
}

TEST(WarriorTank, HoldsThreatWhenThePullTargetAttacksIt)
{
    WarriorTank tank;
    tank.group.StartPull(Tank, Kobold);
    tank.fake.SetVictim(Kobold, Tank);

    tank.Think();

    EXPECT_TRUE(tank.group.TankHoldsThreat());
}

TEST(WarriorTank, TakesDefensiveStanceInTheFight)
{
    WarriorTank tank;
    tank.InFightWithKobold();
    tank.fake.SetAura(Tank, Warrior::DefensiveStance, false);
    tank.fake.SetAura(Tank, Warrior::BattleStance);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::DefensiveStance, Tank)}));
}

TEST(WarriorTank, TauntsTheFocusTargetOffAnotherGroupMember)
{
    WarriorTank tank;
    tank.InFightWithKobold();
    tank.fake.SetVictim(Kobold, Healer);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Taunt, Kobold)}));
}

TEST(WarriorTank, SundersTheFocusTargetWithTheHighestKnownRank)
{
    WarriorTank tank;
    tank.InFightWithKobold();
    tank.fake.SetPower(Tank, 20, 100);
    tank.fake.SetCannotCast(Warrior::Revenge);
    Bots::SpellId const SunderArmor2 = 7405;
    tank.fake.SetSpellChain({Warrior::SunderArmor, SunderArmor2});
    tank.fake.SetKnownSpell(SunderArmor2);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(SunderArmor2, Kobold)}));
}

TEST(WarriorTank, PrefersRevengeWhenItCanCastIt)
{
    WarriorTank tank;
    tank.InFightWithKobold();
    tank.fake.SetPower(Tank, 20, 100);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Revenge, Kobold)}));
}

TEST(WarriorTank, SpendsSpareRageOnHeroicStrike)
{
    WarriorTank tank;
    tank.InFightWithKobold();
    tank.fake.SetPower(Tank, 60, 100);
    tank.fake.SetCannotCast(Warrior::Revenge);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::HeroicStrike, Kobold)}));
}

TEST(WarriorTank, UsesBloodrageWithLittleRage)
{
    WarriorTank tank;
    tank.InFightWithKobold();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Bloodrage, Tank)}));
}

TEST(WarriorTank, EndsThePullWhenTheTargetDies)
{
    WarriorTank tank;
    tank.InFightWithKobold();
    tank.fake.SetHealth(Kobold, 0, 200);

    tank.Think();

    EXPECT_EQ(tank.group.GetPull().phase, PullPhase::None);
    EXPECT_EQ(tank.group.GetFocusTarget(), NoGuid);
}

TEST(WarriorTank, FightsBackAnEnemyThatAttacksIt)
{
    WarriorTank tank;
    tank.fake.SetHealth(Wolf, 300, 300);
    tank.fake.SetVictim(Wolf, Tank);
    tank.fake.SetInCombat(Tank);

    tank.Think();
    tank.Think();

    EXPECT_EQ(tank.group.GetFocusTarget(), Wolf);
    EXPECT_EQ(tank.group.GetPull().phase, PullPhase::None);
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000033", "chase 0xf130000302000033"}));
}
