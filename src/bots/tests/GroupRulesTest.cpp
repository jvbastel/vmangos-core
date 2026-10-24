#include "ClassModules.h"
#include "GroupRules.h"
#include "MageModule.h"
#include "PriestModule.h"
#include "RogueModule.h"
#include "WarriorModule.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

#include <functional>
#include <vector>

using Bots::BotContext;
using Bots::CastKind;
using Bots::DispelKind;
using Bots::GroupCoordinator;
using Bots::NoGuid;
using Bots::NoSpell;
using Bots::StrategyStack;
using Bots::Tests::FakeBot;
namespace Mage = Bots::Mage;
namespace Priest = Bots::Priest;
namespace Rogue = Bots::Rogue;
namespace Warrior = Bots::Warrior;

// The generic group rules (spec 7, Generic rules): interrupts, enemy
// healers, the chase limit, and dispels.
namespace
{
    Bots::Guid const Player = 0x01;
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Healer = 0x12;
    Bots::Guid const RogueBot = 0x13;
    Bots::Guid const MageBot = 0x14;
    Bots::Guid const Shaman = 0xF130000302000022;
    Bots::Guid const Warlock = 0xF130000302000033;
    Bots::Guid const Kobold = 0xF130000302000044;
    Bots::SpellId const HealingWave = 11986;
    Bots::SpellId const LightningBolt = 9532;
    Bots::SpellId const ShadowBolt = 20791;

    std::string CastOn(Bots::SpellId spell, Bots::Guid target)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "cast %u on 0x%016llx", spell, static_cast<unsigned long long>(target));
        return text;
    }

    std::string Deed(char const* verb, Bots::Guid unit)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "%s 0x%016llx", verb, static_cast<unsigned long long>(unit));
        return text;
    }

    // One bot of the party, with its own view of the world.
    struct Bot
    {
        FakeBot fake;
        StrategyStack stack;
        GroupCoordinator& group;

        Bot(Bots::Guid self, Bots::BotClass botClass, Bots::BotRole role, GroupCoordinator& group)
            : fake(self), stack(Bots::MakeStrategyStack(botClass, role)), group(group)
        {
        }

        std::string Think(Bots::Guid attackTarget = NoGuid)
        {
            BotContext const context{fake, fake, group, Bots::CommandState::Follow, Player, attackTarget};
            return stack.Think(context);
        }
    };

    // A level 16 group in a fight: the tank holds a kobold, and the Rogue
    // hits it from behind. All bots see the same world.
    struct Party
    {
        GroupCoordinator group;
        Bot tank{Tank, Bots::BotClass::Warrior, Bots::BotRole::Tank, group};
        Bot rogue{RogueBot, Bots::BotClass::Rogue, Bots::BotRole::Melee, group};
        Bot priest{Healer, Bots::BotClass::Priest, Bots::BotRole::Healer, group};
        Bot mage{MageBot, Bots::BotClass::Mage, Bots::BotRole::Ranged, group};

        Party()
        {
            for (Bots::SpellId spell : {Warrior::DefensiveStance, Warrior::Taunt, Warrior::SunderArmor, Warrior::ShieldBash})
                tank.fake.SetKnownSpell(spell);
            tank.fake.SetAura(Tank, Warrior::DefensiveStance);
            tank.fake.SetPower(Tank, 30, 100);
            for (Bots::SpellId spell : {Rogue::SinisterStrike, Rogue::Kick})
                rogue.fake.SetKnownSpell(spell);
            rogue.fake.SetPower(RogueBot, 100, 100);
            for (Bots::SpellId spell : {Priest::LesserHeal, Priest::Renew})
                priest.fake.SetKnownSpell(spell);
            for (Bots::SpellId spell : {Mage::Frostbolt, Mage::Fireball})
                mage.fake.SetKnownSpell(spell);
            for (Bot* bot : Bots())
            {
                FakeBot& fake = bot->fake;
                fake.SetGroupMembers(Others(fake.GetSelf()));
                for (Bots::Guid unit : {Player, Tank, Healer, RogueBot, MageBot})
                    fake.SetHealth(unit, 400, 400);
                for (Bots::Guid enemy : {Shaman, Warlock, Kobold})
                    fake.SetHealth(enemy, 300, 300);
                fake.SetPower(fake.GetSelf(), 100, 100);
            }
            group.StartPull(Tank, Kobold);
            group.HoldPull(Tank);
            World([](FakeBot& fake) {
                fake.SetVictim(Kobold, Tank);
                fake.SetVictim(Tank, Kobold);
                fake.SetVictim(RogueBot, Kobold);
            });
            rogue.fake.ChaseBehind(Kobold);
            mage.fake.SetDistance(Kobold, 25.0f);
            // The tank tells the group that it is the tank.
            tank.Think();
            ClearDeeds();
        }

        std::vector<Bot*> Bots() { return {&tank, &rogue, &priest, &mage}; }

        std::vector<Bots::Guid> Others(Bots::Guid self)
        {
            std::vector<Bots::Guid> others;
            for (Bots::Guid member : {Tank, Healer, RogueBot, MageBot})
                if (member != self)
                    others.push_back(member);
            return others;
        }

        // Sets a fact of the world in the view of each bot.
        void World(std::function<void(FakeBot&)> const& fact)
        {
            for (Bot* bot : Bots())
                fact(bot->fake);
        }

        void ClearDeeds()
        {
            for (Bot* bot : Bots())
                bot->fake.ClearDeeds();
        }

        // Enemy `enemy` joins the fight: it attacks the tank.
        void Joins(Bots::Guid enemy)
        {
            World([enemy](FakeBot& fake) { fake.SetVictim(enemy, Tank); });
        }

        void Casts(Bots::Guid enemy, Bots::SpellId spell, CastKind kind, bool interruptible = true)
        {
            World([=](FakeBot& fake) { fake.SetCast(enemy, spell, kind, interruptible); });
        }

        void Dies(Bots::Guid enemy)
        {
            World([enemy](FakeBot& fake) {
                fake.SetHealth(enemy, 0, 300);
                fake.SetCast(enemy, NoSpell, CastKind::None);
                fake.SetVictim(enemy, NoGuid);
            });
        }
    };
}

// Interrupts

TEST(GroupRules, TheRogueKicksAHeal)
{
    Party party;
    party.Joins(Shaman);
    party.Casts(Shaman, HealingWave, CastKind::Heal);

    EXPECT_EQ(party.rogue.Think(), "interrupt a heal");

    EXPECT_EQ(party.rogue.fake.Deeds(), std::vector<std::string>{CastOn(Rogue::Kick, Shaman)});
    EXPECT_EQ(party.group.GetInterruptClaim(Shaman, HealingWave), RogueBot);
}

TEST(GroupRules, TheWarriorShieldBashesAHarmfulSpell)
{
    Party party;
    party.Joins(Warlock);
    party.Casts(Warlock, ShadowBolt, CastKind::Harm);

    EXPECT_EQ(party.tank.Think(), "interrupt a harmful spell");

    EXPECT_EQ(party.tank.fake.Deeds(), std::vector<std::string>{CastOn(Warrior::ShieldBash, Warlock)});
}

TEST(GroupRules, HealsComeBeforeHarmfulSpells)
{
    Party party;
    party.Joins(Warlock);
    party.Joins(Shaman);
    party.Casts(Warlock, ShadowBolt, CastKind::Harm);
    party.Casts(Shaman, HealingWave, CastKind::Heal);

    party.rogue.Think();

    EXPECT_EQ(party.rogue.fake.Deeds(), std::vector<std::string>{CastOn(Rogue::Kick, Shaman)});
}

TEST(GroupRules, TwoBotsNeverInterruptTheSameCast)
{
    Party party;
    party.Joins(Shaman);
    party.Casts(Shaman, HealingWave, CastKind::Heal);
    party.rogue.Think();

    EXPECT_NE(party.tank.Think(), "interrupt a heal");

    EXPECT_TRUE(party.tank.fake.Deeds().empty() || party.tank.fake.Deeds()[0] != CastOn(Warrior::ShieldBash, Shaman));
}

TEST(GroupRules, TheOtherBotTakesTheNextCast)
{
    // The Rogue and the Warrior take turns: Kick is on cooldown after the
    // first cast, so Shield Bash stops the second one.
    Party party;
    party.Joins(Shaman);
    party.Casts(Shaman, HealingWave, CastKind::Heal);
    party.rogue.Think();
    // The Kick hits: the cast ends, and a bot drops the claim.
    party.Casts(Shaman, NoSpell, CastKind::None);
    party.rogue.fake.SetCannotCast(Rogue::Kick);
    EXPECT_EQ(party.priest.Think(), "drop an interrupt claim");
    party.ClearDeeds();

    party.Casts(Shaman, HealingWave, CastKind::Heal);

    EXPECT_NE(party.rogue.Think(), "interrupt a heal");
    EXPECT_EQ(party.tank.Think(), "interrupt a heal");
    EXPECT_EQ(party.tank.fake.Deeds(), std::vector<std::string>{CastOn(Warrior::ShieldBash, Shaman)});
}

TEST(GroupRules, ABotThatCannotInterruptGivesUpItsClaim)
{
    // The Kick missed, and the cast goes on: the Warrior may stop it.
    Party party;
    party.Joins(Shaman);
    party.Casts(Shaman, HealingWave, CastKind::Heal);
    party.rogue.Think();
    party.rogue.fake.SetCannotCast(Rogue::Kick);

    EXPECT_EQ(party.rogue.Think(), "drop an interrupt claim");

    EXPECT_EQ(party.group.GetInterruptClaim(Shaman, HealingWave), NoGuid);
    EXPECT_EQ(party.tank.Think(), "interrupt a heal");
}

TEST(GroupRules, TheWarriorNeedsAShieldToInterrupt)
{
    // Without a shield, the bot cannot cast Shield Bash.
    Party party;
    party.tank.fake.SetCannotCast(Warrior::ShieldBash);
    party.Joins(Shaman);
    party.Casts(Shaman, HealingWave, CastKind::Heal);

    EXPECT_NE(party.tank.Think(), "interrupt a heal");
    EXPECT_EQ(party.group.GetInterruptClaim(Shaman, HealingWave), NoGuid);
}

TEST(GroupRules, NoInterruptOfACastThatCannotBeStopped)
{
    Party party;
    party.Joins(Shaman);
    party.Casts(Shaman, HealingWave, CastKind::Heal, false);

    EXPECT_NE(party.rogue.Think(), "interrupt a heal");
}

TEST(GroupRules, NoInterruptOfABuff)
{
    Party party;
    party.Joins(Shaman);
    party.Casts(Shaman, 18968, CastKind::Other);

    std::string const rule = party.rogue.Think();

    EXPECT_NE(rule, "interrupt a heal");
    EXPECT_NE(rule, "interrupt a harmful spell");
}

TEST(GroupRules, NoInterruptOfAnEnemyOutsideTheFight)
{
    Party party;
    party.Casts(Shaman, HealingWave, CastKind::Heal);

    EXPECT_NE(party.rogue.Think(), "interrupt a heal");
}
