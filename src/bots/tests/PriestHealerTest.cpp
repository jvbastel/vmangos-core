#include "ClassModules.h"
#include "PriestModule.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

#include <cstdio>

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

    // A level 16 Troll Priest healer in a group with a tank and a mage, all
    // at full health, with its buffs on everyone.
    struct PriestHealer
    {
        FakeBot fake{Healer};
        GroupCoordinator group;
        StrategyStack stack = Bots::MakeStrategyStack(Bots::BotClass::Priest, Bots::BotRole::Healer);

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
            BotContext const context{fake, fake, group, command, player};
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
