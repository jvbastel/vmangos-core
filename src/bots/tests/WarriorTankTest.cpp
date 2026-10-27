#include "ClassModules.h"
#include "DamageRules.h"
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

// The full run 12 of jvbastel/WOWAI#103 at pack4: the charge landed, the
// Earthborer did not fight, and the next pull of it waited 60 s. The tank
// still attacked the pull target (its victim since the charge), out of
// combat, with no chase and Charge not castable (cooldown, or the target
// nearer than its least range): no pull rule fired.
TEST(WarriorTank, RunsToThePullTargetItAttacksWithoutAFight)
{
    WarriorTank tank;
    tank.fake.SetCannotCast(Warrior::Charge);
    tank.fake.SetVictim(Tank, Kobold);
    tank.fake.SetInCombat(Tank, false);
    tank.group.StartPull(Tank, Kobold);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000022", "chase 0xf130000302000022"}));
}

TEST(WarriorTank, RunsToThePullTargetOnlyOnce)
{
    WarriorTank tank;
    tank.fake.SetCannotCast(Warrior::Charge);
    tank.group.StartPull(Tank, Kobold);
    tank.Think();
    tank.fake.SetVictim(Tank, Kobold);
    tank.fake.SetInCombat(Tank, false);
    tank.fake.ClearDeeds();

    tank.Think();

    EXPECT_TRUE(tank.fake.Deeds().empty());
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

// The focus died while a second enemy hits the healer: the tank takes it as
// its new focus and taunts it off at once, before it walks to it. The walk
// first left the healer under the hits for seconds more (WOWAI#73 review).
TEST(WarriorTank, TauntsANewFocusOffAMemberBeforeItWalksToIt)
{
    WarriorTank tank;
    tank.fake.SetGroupMembers({Healer});
    tank.fake.SetInCombat(Tank);
    tank.fake.SetAura(Tank, Warrior::BattleStance, false);
    tank.fake.SetAura(Tank, Warrior::DefensiveStance);
    tank.fake.SetAura(Tank, Warrior::BattleShout);
    tank.fake.SetHealth(Healer, 300, 300);
    tank.fake.SetHealth(Wolf, 300, 300);
    tank.fake.SetVictim(Wolf, Healer);
    tank.group.SetFocusTarget(Wolf);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Taunt, Wolf)}));
}

namespace
{
    // Outside a pull, a new focus hits the healer, 15 yards from the tank.
    struct FocusOnTheHealer : WarriorTank
    {
        FocusOnTheHealer()
        {
            fake.SetGroupMembers({Healer});
            fake.SetInCombat(Tank);
            fake.SetAura(Tank, Warrior::BattleStance, false);
            fake.SetAura(Tank, Warrior::DefensiveStance);
            fake.SetAura(Tank, Warrior::BattleShout);
            fake.SetHealth(Healer, 300, 300);
            fake.SetHealth(Wolf, 300, 300);
            fake.SetDistance(Wolf, 15.0f);
            fake.SetVictim(Wolf, Healer);
            group.SetFocusTarget(Wolf);
        }
    };
}

// In taunt range, the taunt cannot go out (it was resisted and waits for
// its cooldown): the tank tells the damage bots that no taunt comes, so
// they attack the focus (WOWAI#93 review).
TEST(WarriorTank, TellsTheGroupWhenTheTauntCannotCome)
{
    FocusOnTheHealer tank;
    tank.fake.SetCannotCast(Warrior::Taunt);

    tank.Think();

    EXPECT_TRUE(tank.group.IsTauntGivenUp(Wolf));
}

TEST(WarriorTank, ATankWithoutTauntTellsTheGroupToo)
{
    FocusOnTheHealer tank;
    tank.fake.SetKnownSpell(Warrior::Taunt, false);

    tank.Think();

    EXPECT_TRUE(tank.group.IsTauntGivenUp(Wolf));
}

TEST(WarriorTank, AFocusOutOfTauntRangeStillGetsItsTaunt)
{
    FocusOnTheHealer tank;
    tank.fake.SetCannotCast(Warrior::Taunt);
    tank.fake.SetDistance(Wolf, 35.0f);

    tank.Think();

    EXPECT_FALSE(tank.group.IsTauntGivenUp(Wolf));
}

TEST(WarriorTank, AReadyTauntIsNoGiveUp)
{
    FocusOnTheHealer tank;

    tank.Think();

    EXPECT_FALSE(tank.group.IsTauntGivenUp(Wolf));
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Taunt, Wolf)}));
}

TEST(WarriorTank, TauntsAnotherEnemyOffAGroupMember)
{
    WarriorTank tank;
    tank.fake.SetGroupMembers({Healer});
    tank.InFightWithKobold();
    tank.fake.SetInCombat(Tank);
    tank.fake.SetHealth(Healer, 300, 300);
    tank.fake.SetHealth(Wolf, 300, 300);
    tank.fake.SetVictim(Wolf, Healer);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Taunt, Wolf)}));
    EXPECT_EQ(tank.group.GetFocusTarget(), Kobold);
}

TEST(WarriorTank, TauntsNoDeadEnemyOffAGroupMember)
{
    WarriorTank tank;
    tank.fake.SetGroupMembers({Healer});
    tank.InFightWithKobold();
    tank.fake.SetInCombat(Tank);
    tank.fake.SetHealth(Healer, 300, 300);
    tank.fake.SetHealth(Wolf, 0, 300);
    tank.fake.SetVictim(Wolf, Healer);

    tank.Think();

    for (std::string const& deed : tank.fake.Deeds())
        EXPECT_NE(deed, CastOn(Warrior::Taunt, Wolf));
}

// A dead member keeps its attackers until the core clears them: the tank
// takes no enemy off a dead member.
TEST(WarriorTank, TauntsNoEnemyOffADeadMember)
{
    WarriorTank tank;
    tank.fake.SetGroupMembers({Healer});
    tank.InFightWithKobold();
    tank.fake.SetInCombat(Tank);
    tank.fake.SetHealth(Healer, 0, 300);
    tank.fake.SetHealth(Wolf, 300, 300);
    tank.fake.SetVictim(Wolf, Healer);

    tank.Think();

    for (std::string const& deed : tank.fake.Deeds())
        EXPECT_NE(deed, CastOn(Warrior::Taunt, Wolf));
}

TEST(WarriorTank, TauntsNoEnemyOffItself)
{
    WarriorTank tank;
    tank.fake.SetGroupMembers({Healer});
    tank.InFightWithKobold();
    tank.fake.SetInCombat(Tank);
    tank.fake.SetHealth(Healer, 300, 300);
    tank.fake.SetHealth(Wolf, 300, 300);
    tank.fake.SetVictim(Wolf, Tank);

    tank.Think();

    for (std::string const& deed : tank.fake.Deeds())
        EXPECT_NE(deed, CastOn(Warrior::Taunt, Wolf));
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

TEST(WarriorTank, TakesTheEnemyOnAMemberAsFocusWhenTheFocusDies)
{
    WarriorTank tank;
    tank.fake.SetGroupMembers({Healer});
    tank.InFightWithKobold();
    tank.fake.SetInCombat(Tank);
    tank.fake.SetHealth(Healer, 300, 300);
    tank.fake.SetHealth(Wolf, 300, 300);
    tank.fake.SetVictim(Wolf, Healer);
    tank.fake.SetHealth(Kobold, 0, 200);
    tank.fake.SetVictim(Kobold, NoGuid);

    tank.Think();
    tank.Think();

    EXPECT_EQ(tank.group.GetFocusTarget(), Wolf);
    EXPECT_EQ(tank.group.GetPull().phase, PullPhase::None);

    // A damage bot of the group, attacked by nothing, follows the tank.
    Bots::Guid const Rogue = 0x13;
    FakeBot rogue{Rogue};
    rogue.SetGroupMembers({Tank, Healer});
    rogue.SetHealth(Rogue, 400, 400);
    rogue.SetHealth(Wolf, 300, 300);
    rogue.SetVictim(Wolf, Healer);
    BotContext const context{rogue, rogue, tank.group, Bots::CommandState::Follow, 0x1};
    EXPECT_EQ(Bots::PickDamageTarget(context), Wolf);
}
