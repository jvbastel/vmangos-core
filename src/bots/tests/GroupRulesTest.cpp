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
                fake.SetInCombat(Healer);
                fake.SetInCombat(MageBot);
            });
            rogue.fake.ChaseBehind(Kobold);
            mage.fake.SetDistance(Kobold, 25.0f);
            // The bot AI of the tank tells it.
            group.SetTank(Tank);
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

// Enemy healers

TEST(GroupRules, ABotMarksAnEnemyThatCastsAHeal)
{
    Party party;
    party.Joins(Shaman);
    party.Casts(Shaman, HealingWave, CastKind::Heal);

    EXPECT_EQ(party.priest.Think(), "mark an enemy healer");

    EXPECT_EQ(party.group.GetEnemyHealers(), std::vector<Bots::Guid>{Shaman});
}

TEST(GroupRules, ADamageBotSwitchesToAnEnemyHealerAndBackAfterItDies)
{
    Party party;
    party.rogue.fake.SetCannotCast(Rogue::Kick);
    party.Joins(Shaman);
    party.Casts(Shaman, HealingWave, CastKind::Heal);
    party.priest.Think();

    EXPECT_EQ(party.rogue.Think(), "attack the target");
    ASSERT_FALSE(party.rogue.fake.Deeds().empty());
    EXPECT_EQ(party.rogue.fake.Deeds()[0], Deed("attack", Shaman));
    party.rogue.fake.SetVictim(RogueBot, Shaman);
    party.rogue.fake.ClearDeeds();

    // The heal ends: the Rogue stays on the healer.
    party.Casts(Shaman, NoSpell, CastKind::None);
    EXPECT_NE(party.rogue.Think(), "attack the target");

    party.Dies(Shaman);
    party.rogue.fake.ClearDeeds();
    party.priest.Think(); // It forgets the dead healer.
    EXPECT_EQ(party.rogue.Think(), "attack the target");
    ASSERT_FALSE(party.rogue.fake.Deeds().empty());
    EXPECT_EQ(party.rogue.fake.Deeds()[0], Deed("attack", Kobold));
}

TEST(GroupRules, ARangedBotCastsOnTheEnemyHealer)
{
    Party party;
    party.Joins(Shaman);
    party.Casts(Shaman, HealingWave, CastKind::Heal);
    party.priest.Think();
    party.Casts(Shaman, NoSpell, CastKind::None);

    EXPECT_EQ(party.mage.Think(), "frostbolt");

    EXPECT_EQ(party.mage.fake.Deeds(), std::vector<std::string>{CastOn(Mage::Frostbolt, Shaman)});
}

TEST(GroupRules, TheAttackTargetOfThePlayerComesBeforeTheEnemyHealer)
{
    Party party;
    party.Joins(Shaman);
    party.Joins(Warlock);
    party.Casts(Shaman, HealingWave, CastKind::Heal);
    party.priest.Think();
    party.Casts(Shaman, NoSpell, CastKind::None);

    EXPECT_EQ(party.mage.Think(Warlock), "frostbolt");

    EXPECT_EQ(party.mage.fake.Deeds(), std::vector<std::string>{CastOn(Mage::Frostbolt, Warlock)});
}

TEST(GroupRules, ABotForgetsADeadEnemyHealer)
{
    Party party;
    party.Joins(Shaman);
    party.Casts(Shaman, HealingWave, CastKind::Heal);
    party.priest.Think();

    party.Dies(Shaman);

    EXPECT_EQ(party.priest.Think(), "forget an enemy healer");
    EXPECT_TRUE(party.group.GetEnemyHealers().empty());
}

// Chase limit

namespace
{
    // The kobold runs away from the tank: it attacks nobody, and it is
    // `yards` from the tank.
    void Flees(Party& party, float yards)
    {
        party.World([yards](FakeBot& fake) {
            fake.SetVictim(Kobold, NoGuid);
            fake.SetInCombat(Kobold);
            fake.SetDistanceBetween(Kobold, Tank, yards);
        });
    }
}

TEST(GroupRules, AMeleeBotChasesAFleeingEnemyUpToTheLimit)
{
    Party party;
    Flees(party, 18.0f);

    EXPECT_NE(party.rogue.Think(), "come back to the tank");
}

TEST(GroupRules, AMeleeBotComesBackToTheTankFromAFleeingEnemy)
{
    Party party;
    Flees(party, 22.0f);

    EXPECT_EQ(party.rogue.Think(), "come back to the tank");

    EXPECT_EQ(party.rogue.fake.Deeds(), std::vector<std::string>{Deed("follow", Tank)});
}

TEST(GroupRules, ABotThatCameBackDoesNotChaseTheFleeingEnemyAgain)
{
    Party party;
    Flees(party, 22.0f);
    party.rogue.Think();
    party.rogue.fake.SetFollowTarget(Tank);
    party.rogue.fake.ClearDeeds();

    party.rogue.Think();

    EXPECT_TRUE(party.rogue.fake.Deeds().empty());
}

TEST(GroupRules, ABotChasesTheEnemyAgainWhenItComesBack)
{
    Party party;
    Flees(party, 22.0f);
    party.rogue.Think();
    party.rogue.fake.ClearDeeds();

    party.World([](FakeBot& fake) {
        fake.SetVictim(Kobold, Tank);
        fake.SetDistanceBetween(Kobold, Tank, 3.0f);
    });

    EXPECT_NE(party.rogue.Think(), "");
    EXPECT_EQ(party.rogue.fake.Deeds(), std::vector<std::string>{Deed("chase behind", Kobold)});
}

TEST(GroupRules, ARangedBotComesBackAndStopsCastingOnAFleeingEnemy)
{
    Party party;
    party.mage.fake.ChaseToRange(Kobold, 25.0f);
    party.mage.fake.ClearDeeds();
    Flees(party, 22.0f);

    EXPECT_EQ(party.mage.Think(), "come back to the tank");
    party.mage.fake.ClearDeeds();
    party.mage.Think();

    EXPECT_EQ(party.mage.fake.Deeds(), std::vector<std::string>{});
}

TEST(GroupRules, ABotBringsAnEnemyThatAttacksItFarFromTheTankBackToTheTank)
{
    // A fleeing enemy can keep the bot as its victim, so the limit is the
    // same for an enemy that attacks the bot: it follows the bot to the
    // tank, and the bot fights it there.
    Party party;
    party.World([](FakeBot& fake) {
        fake.SetVictim(Warlock, RogueBot);
        fake.SetDistanceBetween(Warlock, Tank, 30.0f);
    });
    party.rogue.fake.SetVictim(RogueBot, Warlock);
    party.rogue.fake.Chase(Warlock);
    party.rogue.fake.ClearDeeds();
    Flees(party, 22.0f);

    EXPECT_EQ(party.rogue.Think(), "come back to the tank");
    party.rogue.fake.SetFollowTarget(Tank);
    party.rogue.fake.ClearDeeds();
    party.rogue.Think();
    EXPECT_TRUE(party.rogue.fake.Deeds().empty());

    party.World([](FakeBot& fake) { fake.SetDistanceBetween(Warlock, Tank, 4.0f); });
    party.rogue.Think();
    EXPECT_EQ(party.rogue.fake.Deeds(), std::vector<std::string>{Deed("chase", Warlock)});
}

TEST(GroupRules, TheAttackTargetOfThePlayerHasNoChaseLimit)
{
    Party party;
    Flees(party, 22.0f);

    EXPECT_NE(party.rogue.Think(Kobold), "come back to the tank");
}

TEST(GroupRules, NoChaseLimitWithoutALivingTank)
{
    Party party;
    party.World([](FakeBot& fake) { fake.SetHealth(Tank, 0, 400); });
    Flees(party, 22.0f);

    EXPECT_NE(party.rogue.Think(), "come back to the tank");
}

TEST(GroupRules, TheTankHasNoChaseLimit)
{
    Party party;
    party.tank.fake.Chase(Kobold);
    Flees(party, 22.0f);

    EXPECT_NE(party.tank.Think(), "come back to the tank");
}

// Dispel

TEST(GroupRules, ALevel16GroupDispelsNothing)
{
    // No bot knows a dispel spell before level 18 (Cure Disease: 14).
    Party party;
    party.World([](FakeBot& fake) {
        fake.SetDebuff(Tank, DispelKind::Magic);
        fake.SetDebuff(Tank, DispelKind::Curse);
    });

    for (Bot* bot : party.Bots())
        EXPECT_NE(bot->Think(), "dispel a debuff");
}

TEST(GroupRules, APriestThatKnowsDispelMagicDispelsAMagicDebuff)
{
    Party party;
    party.priest.fake.SetKnownSpell(Priest::DispelMagic);
    party.priest.fake.SetDebuff(Tank, DispelKind::Magic);

    EXPECT_EQ(party.priest.Think(), "dispel a debuff");

    EXPECT_EQ(party.priest.fake.Deeds(), std::vector<std::string>{CastOn(Priest::DispelMagic, Tank)});
}

TEST(GroupRules, AMageThatKnowsRemoveLesserCurseRemovesACurseOnly)
{
    Party party;
    party.mage.fake.SetKnownSpell(Mage::RemoveLesserCurse);
    party.mage.fake.SetDebuff(Tank, DispelKind::Magic);
    EXPECT_NE(party.mage.Think(), "dispel a debuff");
    party.mage.fake.ClearDeeds();

    party.mage.fake.SetDebuff(RogueBot, DispelKind::Curse);

    EXPECT_EQ(party.mage.Think(), "dispel a debuff");
    EXPECT_EQ(party.mage.fake.Deeds(), std::vector<std::string>{CastOn(Mage::RemoveLesserCurse, RogueBot)});
}

TEST(GroupRules, APriestCuresADisease)
{
    Party party;
    party.priest.fake.SetKnownSpell(Priest::CureDisease);
    party.priest.fake.SetDebuff(MageBot, DispelKind::Disease);

    EXPECT_EQ(party.priest.Think(), "dispel a debuff");

    EXPECT_EQ(party.priest.fake.Deeds(), std::vector<std::string>{CastOn(Priest::CureDisease, MageBot)});
}

TEST(GroupRules, APriestHealsABadlyHurtMemberBeforeItDispels)
{
    Party party;
    party.priest.fake.SetKnownSpell(Priest::DispelMagic);
    party.priest.fake.SetDebuff(Tank, DispelKind::Magic);
    party.priest.fake.SetHealth(Tank, 150, 400);

    EXPECT_NE(party.priest.Think(), "dispel a debuff");
}

TEST(GroupRules, NoBotRemovesPoison)
{
    Party party;
    party.priest.fake.SetKnownSpell(Priest::DispelMagic);
    party.priest.fake.SetKnownSpell(Priest::CureDisease);
    party.priest.fake.SetDebuff(Tank, DispelKind::Poison);

    EXPECT_NE(party.priest.Think(), "dispel a debuff");
}
