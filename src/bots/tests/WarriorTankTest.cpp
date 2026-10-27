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
            fake.SetDistance(Kobold, 2.0f);
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

namespace
{
    std::uint32_t const WornShortbow = 2504;
    std::uint32_t const RoughArrow = 2512;
    std::uint32_t const RazorArrow = 3030;
    Bots::SpellId const ShootBow = 2480;

    Bots::RangedWeapon Shortbow(std::uint32_t arrows, std::uint32_t arrow = RoughArrow)
    {
        Bots::RangedWeapon weapon;
        weapon.kind = Bots::RangedKind::Bow;
        weapon.entry = WornShortbow;
        weapon.ammoEntry = arrows ? arrow : 0;
        weapon.ammoCount = arrows;
        return weapon;
    }

    // A tank with a bow and arrows at (0, 0, 0), told to pull the kobold
    // 20 yards away. Its first think notes where it stands.
    struct BowTank : WarriorTank
    {
        BowTank()
        {
            fake.SetKnownSpell(ShootBow);
            fake.SetRangedWeapon(Shortbow(200));
            fake.SetPosition(Tank, {0, 0, 0});
        }

        void Pull()
        {
            group.StartPull(Tank, Kobold);
            Think();
            fake.ClearDeeds();
        }
    };
}

TEST(WarriorTank, NotesWhereItStandsWhenAPullBegins)
{
    BowTank tank;
    tank.fake.SetPosition(Tank, {3, 4, 5});
    tank.group.StartPull(Tank, Kobold);

    tank.Think();

    Bots::PullState const pull = tank.group.GetPull();
    EXPECT_TRUE(pull.hasSpot);
    EXPECT_EQ(3.0f, pull.spot.x);
    EXPECT_EQ(4.0f, pull.spot.y);
    EXPECT_TRUE(tank.fake.Deeds().empty());
}

TEST(WarriorTank, ShootsThePullTargetFromRangeInsteadOfCharging)
{
    BowTank tank;
    tank.Pull();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(ShootBow, Kobold)}));
}

TEST(WarriorTank, GoesIntoShootingRangeOfAFarPullTarget)
{
    BowTank tank;
    tank.fake.SetDistance(Kobold, 45.0f);
    tank.Pull();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000022 to 25 yards"}));
}

TEST(WarriorTank, GoesIntoShootingRangeWhenItCannotSeeThePullTarget)
{
    BowTank tank;
    tank.fake.AddBlindToFromX(Kobold, -1.0f);
    tank.Pull();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000022 to 25 yards"}));
}

TEST(WarriorTank, WaitsWhileItsShotGoesOut)
{
    BowTank tank;
    tank.Pull();
    tank.fake.SetCurrentCast(Tank, ShootBow);
    tank.fake.SetCannotCast(ShootBow);

    tank.Think();

    EXPECT_TRUE(tank.fake.Deeds().empty());
}

TEST(WarriorTank, WaitsForItsNextShotRatherThanRunningIn)
{
    BowTank tank;
    tank.Pull();
    tank.fake.SetCannotCast(ShootBow);

    tank.Think();

    EXPECT_TRUE(tank.fake.Deeds().empty());
}

TEST(WarriorTank, WithNoRangedWeaponRunsInToHitThePullTarget)
{
    BowTank tank;
    tank.fake.SetRangedWeapon(Bots::RangedWeapon{});
    tank.Pull();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000022", "chase 0xf130000302000022"}));
}

TEST(WarriorTank, WithNoAmmoRunsInToHitThePullTarget)
{
    BowTank tank;
    tank.fake.SetRangedWeapon(Shortbow(0));
    tank.Pull();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000022", "chase 0xf130000302000022"}));
}

TEST(WarriorTank, TooCloseToShootRunsInToHitThePullTarget)
{
    BowTank tank;
    tank.fake.SetDistance(Kobold, 6.0f);
    tank.Pull();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000022", "chase 0xf130000302000022"}));
}

TEST(WarriorTank, NeverChargesThePullTarget)
{
    BowTank tank;
    tank.fake.SetRangedWeapon(Bots::RangedWeapon{});
    tank.Pull();

    for (int think = 0; think < 3; ++think)
        tank.Think();

    for (std::string const& deed : tank.fake.Deeds())
        EXPECT_EQ(std::string::npos, deed.find(CastOn(Warrior::Charge, Kobold))) << deed;
}

// The fallback pull: the tank ran in and hit the kobold once. Now the
// kobold attacks it, and the tank walks back to where it started.
TEST(WarriorTank, WalksBackToWhereItStartedAfterTheHit)
{
    BowTank tank;
    tank.fake.SetRangedWeapon(Bots::RangedWeapon{});
    tank.fake.SetPosition(Tank, {10, 20, 0});
    tank.Pull();
    tank.fake.SetPosition(Tank, {28, 20, 0});
    tank.fake.SetDistance(Kobold, 2.0f);
    tank.fake.SetInCombat(Tank);
    tank.fake.SetVictim(Tank, Kobold);
    tank.fake.SetVictim(Kobold, Tank);

    tank.Think(); // Holds the pull.
    tank.Think();

    EXPECT_EQ(Bots::PullPhase::Holding, tank.group.GetPull().phase);
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"keep move to 10 20 0"}));
}

TEST(WarriorTank, BackAtItsSpotItStopsWalkingBack)
{
    BowTank tank;
    tank.Pull();
    tank.fake.SetPosition(Tank, {1, 0, 0});
    tank.fake.SetInCombat(Tank);
    tank.fake.SetVictim(Kobold, Tank);
    tank.Think(); // Holds the pull.

    tank.Think();

    EXPECT_FALSE(tank.group.GetPull().hasSpot);
}

// After the shot, the kobold runs to the tank: the tank waits for it where
// it stands, and does not run to meet it.
TEST(WarriorTank, WaitsForThePulledTargetToComeToIt)
{
    BowTank tank;
    tank.Pull();
    tank.fake.SetInCombat(Tank);
    tank.fake.SetVictim(Kobold, Tank);
    tank.fake.SetMoving(Kobold);
    tank.fake.SetDistance(Kobold, 15.0f);
    tank.group.HoldPull(Tank);
    tank.group.ClearPullSpot(Tank);
    tank.fake.SetAura(Tank, Warrior::BattleStance, false);
    tank.fake.SetAura(Tank, Warrior::DefensiveStance);
    tank.fake.SetAura(Tank, Warrior::BattleShout);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000022", "stand"}));
}

// The tank went into shooting range with a ranged chase, which attacks
// without a melee swing. The kobold came: the tank hits it in melee now.
TEST(WarriorTank, AfterARangedPullMeleesTheFocus)
{
    BowTank tank;
    tank.Pull();
    tank.fake.ChaseToRange(Kobold, 25.0f);
    tank.fake.ClearDeeds();
    tank.fake.SetInCombat(Tank);
    tank.fake.SetVictim(Kobold, Tank);
    tank.fake.SetVictim(Tank, Kobold);
    tank.fake.SetDistance(Kobold, 2.0f);
    tank.group.HoldPull(Tank);
    tank.group.ClearPullSpot(Tank);
    tank.fake.SetAura(Tank, Warrior::BattleStance, false);
    tank.fake.SetAura(Tank, Warrior::DefensiveStance);
    tank.fake.SetAura(Tank, Warrior::BattleShout);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000022", "chase 0xf130000302000022"}));
}

// An enemy that attacks the tank but stays away, such as a caster, gets
// the tank's chase as before.
TEST(WarriorTank, RunsToAFocusThatStaysAway)
{
    BowTank tank;
    tank.Pull();
    tank.fake.SetInCombat(Tank);
    tank.fake.SetVictim(Kobold, Tank);
    tank.fake.SetVictim(Tank, Kobold);
    tank.fake.SetDistance(Kobold, 15.0f);
    tank.group.HoldPull(Tank);
    tank.group.ClearPullSpot(Tank);
    tank.fake.SetAura(Tank, Warrior::BattleStance, false);
    tank.fake.SetAura(Tank, Warrior::DefensiveStance);
    tank.fake.SetAura(Tank, Warrior::BattleShout);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000022", "chase 0xf130000302000022"}));
}

TEST(WarriorTank, TopsUpItsArrowsOutOfCombatWithoutBuying)
{
    BowTank tank;
    tank.fake.SetLevel(Tank, 30);
    tank.fake.SetRangedWeapon(Shortbow(20, RazorArrow));

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"top up ammo 3030 to 200"}));
}

TEST(WarriorTank, WithFullBagsTriesNoAmmoTopUp)
{
    BowTank tank;
    tank.fake.SetLevel(Tank, 30);
    tank.fake.SetRangedWeapon(Shortbow(20, RazorArrow));
    tank.fake.SetBagsFull();

    tank.Think();

    for (std::string const& deed : tank.fake.Deeds())
        EXPECT_EQ(std::string::npos, deed.find("top up")) << deed;
}

TEST(WarriorTank, DoesNotTopUpItsAmmoInAFight)
{
    BowTank tank;
    tank.fake.SetLevel(Tank, 30);
    tank.fake.SetRangedWeapon(Shortbow(20, RazorArrow));
    tank.InFightWithKobold();

    tank.Think();

    for (std::string const& deed : tank.fake.Deeds())
        EXPECT_EQ(std::string::npos, deed.find("top up")) << deed;
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
