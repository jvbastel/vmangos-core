#include "ClassModules.h"
#include "DamageRules.h"
#include "KeepOut.h"
#include "WarriorModule.h"
#include "FakeBot.h"
#include "MemberSpot.h"

#include <gtest/gtest.h>

#include <algorithm>

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
        // The clock of the think (BotContext::nowMs).
        std::int64_t nowMs = 0;

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
            BotContext context{fake, fake, group, command, player};
            context.nowMs = nowMs;
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
    EXPECT_TRUE(pull.hasFightSpot);
    EXPECT_EQ(3.0f, pull.fightSpot.x);
    EXPECT_EQ(4.0f, pull.fightSpot.y);
    EXPECT_TRUE(tank.fake.Deeds().empty());
}

TEST(WarriorTank, ShootsThePullTargetFromRangeInsteadOfCharging)
{
    BowTank tank;
    tank.Pull();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"stand", CastOn(ShootBow, Kobold)}));
}

TEST(WarriorTank, GoesIntoShootingRangeOfAFarPullTarget)
{
    BowTank tank;
    tank.fake.SetDistance(Kobold, 45.0f);
    // No safe place to shoot from (no ground path): the ranged chase.
    tank.fake.AddNoPathFromX(-1000.0f);
    tank.Pull();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000022 to 25 yards"}));
}

// In range but out of sight: the tank walks to a safe place round the
// target from which it sees it (FightSpot.h), not into the pack.
TEST(WarriorTank, GoesToASafePlaceToShootFromWhenItCannotSeeThePullTarget)
{
    BowTank tank;
    tank.fake.SetPosition(Kobold, {20, 0, 0});
    tank.fake.AddBlindToFromX(Kobold, -1.0f);
    tank.Pull();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"keep move to -7 0 0"}));
}

// With no place to shoot from, the tank closes in until it sees the target:
// a ranged chase, whose path ends at the first point in range and in sight.
// Not a melee chase: the core moves a chase only toward the bot's victim, so
// a melee chase with no attack never moved (jvbastel/WOWAI#122, two_packs).
TEST(WarriorTank, ClosesInWithARangedChaseWhenItCannotSeeThePullTargetFromAnywhere)
{
    BowTank tank;
    tank.fake.AddBlindToFromX(Kobold, -1000.0f);
    tank.Pull();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000022 to 25 yards"}));
    EXPECT_EQ(tank.fake.GetVictim(Tank), Kobold);
    EXPECT_FALSE(tank.fake.IsMeleeAttacking());
}

// The chase into shooting range ended, and the tank still cannot shoot (a
// target out of reach of the path): it runs in instead.
TEST(WarriorTank, RunsInWhenItsChaseIntoRangeEndsOutOfShot)
{
    BowTank tank;
    tank.fake.SetDistance(Kobold, 45.0f);
    // No safe place to shoot from (no ground path): the ranged chase.
    tank.fake.AddNoPathFromX(-1000.0f);
    tank.Pull();
    tank.fake.SetMoving(Tank);
    for (int think = 0; think < 3; ++think)
        tank.Think();
    tank.fake.SetMoving(Tank, false);
    for (int think = 0; think < 3; ++think)
        tank.Think();
    tank.fake.ClearDeeds();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000022", "chase 0xf130000302000022"}));
}

// The think right after the chase order: the chase is on, but the tank does
// not move yet. That is no end of the chase (the two_packs scenario of
// jvbastel/WOWAI#105, where the tank ran in each time).
TEST(WarriorTank, KeepsChasingOnTheThinkAfterItsChaseOrder)
{
    BowTank tank;
    tank.fake.AddBlindToFromX(Kobold, -1000.0f);
    tank.Pull();
    tank.Think();
    ASSERT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000022 to 25 yards"}));
    tank.fake.ClearDeeds();
    tank.fake.SetMoving(Tank, false);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000022 to 25 yards"}));
}

// A chase that moved and pauses for one think (a new path) has not ended:
// only a few thinks in a row with no move end it.
TEST(WarriorTank, KeepsChasingWhenItsMovingChasePausesForOneThink)
{
    BowTank tank;
    tank.fake.AddBlindToFromX(Kobold, -1000.0f);
    tank.Pull();
    tank.fake.SetMoving(Tank);
    for (int think = 0; think < 5; ++think)
        tank.Think();
    tank.fake.ClearDeeds();
    tank.fake.SetMoving(Tank, false);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000022 to 25 yards"}));
}

// A chase that never moves (the path gets no nearer) ends after a few
// thinks: the tank runs in and does not wait for good.
TEST(WarriorTank, RunsInWhenItsChaseNeverMoves)
{
    BowTank tank;
    tank.fake.AddBlindToFromX(Kobold, -1000.0f);
    tank.Pull();
    tank.fake.SetMoving(Tank, false);
    for (int think = 0; think < 3; ++think)
        tank.Think();
    tank.fake.ClearDeeds();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000022", "chase 0xf130000302000022"}));
}

TEST(WarriorTank, KeepsChasingIntoRangeWhileItMoves)
{
    BowTank tank;
    tank.fake.SetDistance(Kobold, 45.0f);
    // No safe place to shoot from (no ground path): the ranged chase.
    tank.fake.AddNoPathFromX(-1000.0f);
    tank.Pull();
    tank.fake.ChaseToRange(Kobold, 25.0f);
    tank.fake.ClearDeeds();
    tank.fake.SetMoving(Tank);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000022 to 25 yards"}));
}

// The shot is out and the tank is in combat, but the kobold does not
// attack it yet: the tank stays and does not run to it.
TEST(WarriorTank, DoesNotRunToThePullTargetAfterItsShot)
{
    BowTank tank;
    tank.Pull();
    tank.fake.SetInCombat(Tank);
    tank.fake.SetCannotCast(ShootBow);
    tank.fake.SetAura(Tank, Warrior::BattleStance, false);
    tank.fake.SetAura(Tank, Warrior::DefensiveStance);
    tank.fake.SetAura(Tank, Warrior::BattleShout);

    tank.Think();

    for (std::string const& deed : tank.fake.Deeds())
    {
        EXPECT_EQ(std::string::npos, deed.find("attack")) << deed;
        EXPECT_EQ(std::string::npos, deed.find("chase")) << deed;
    }
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

// Too near to shoot, and no place to shoot from (no ground path): the
// tank runs in.
TEST(WarriorTank, TooCloseToShootRunsInToHitThePullTarget)
{
    BowTank tank;
    tank.fake.SetDistance(Kobold, 6.0f);
    tank.fake.AddNoPathFromX(-1000.0f);
    tank.Pull();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000022", "chase 0xf130000302000022"}));
}

// The full run 12 of jvbastel/WOWAI#103 at pack4: the tank attacked the pull
// target (its melee victim from an earlier run-in or charge), but no fight
// started, and the next pull of it waited 60 s. Out of combat, no chase, the
// target 3.5 yd away (too near to shoot), and the target attacks nobody.
TEST(WarriorTank, RunsToThePullTargetItAttacksWithoutAFight)
{
    BowTank tank;
    tank.fake.SetDistance(Kobold, 3.5f);
    tank.Pull();
    tank.fake.SetVictim(Tank, Kobold);
    tank.fake.SetInCombat(Tank, false);
    tank.fake.ClearDeeds();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"attack 0xf130000302000022", "chase 0xf130000302000022"}));
}

TEST(WarriorTank, RunsToThePullTargetOnlyOnce)
{
    BowTank tank;
    tank.fake.SetDistance(Kobold, 3.5f);
    tank.fake.AddNoPathFromX(-1000.0f);
    tank.Pull();
    tank.Think();
    tank.fake.SetVictim(Tank, Kobold);
    tank.fake.SetInCombat(Tank, false);
    tank.fake.ClearDeeds();

    tank.Think();

    EXPECT_TRUE(tank.fake.Deeds().empty());
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
    // The opener is done: it fires on the way otherwise.
    tank.fake.SetAura(Tank, Warrior::BattleStance, false);
    tank.fake.SetAura(Tank, Warrior::DefensiveStance);
    tank.fake.SetPower(Tank, 20, 100);

    tank.Think(); // Holds the pull.
    tank.Think();

    EXPECT_EQ(Bots::PullPhase::Holding, tank.group.GetPull().phase);
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"keep move to 10 20 0"}));
}

// An enemy of the pack hits a member while the tank walks back: the tank
// taunts it off first, and walks on after.
TEST(WarriorTank, TauntsAnEnemyOffAMemberBeforeItWalksBack)
{
    BowTank tank;
    tank.fake.SetRangedWeapon(Bots::RangedWeapon{});
    tank.fake.SetGroupMembers({Healer});
    tank.fake.SetPosition(Tank, {10, 20, 0});
    tank.Pull();
    tank.fake.SetPosition(Tank, {28, 20, 0});
    tank.fake.SetDistance(Kobold, 2.0f);
    tank.fake.SetInCombat(Tank);
    tank.fake.SetVictim(Tank, Kobold);
    tank.fake.SetVictim(Kobold, Tank);
    tank.fake.SetAura(Tank, Warrior::DefensiveStance);
    tank.fake.SetAura(Tank, Warrior::BattleShout);
    tank.fake.SetHealth(Healer, 300, 300);
    tank.fake.SetHealth(Wolf, 300, 300);
    tank.fake.SetDistance(Wolf, 10.0f);
    tank.fake.SetVictim(Wolf, Healer);

    tank.Think(); // Holds the pull.
    // The wolf joined the pull: the tank warns first (jvbastel/WOWAI#121).
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"warn Careful: this pull brings 1 more pack."}));
    tank.fake.ClearDeeds();
    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Taunt, Wolf)}));
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

    EXPECT_FALSE(tank.group.GetPull().hasFightSpot);
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
    tank.group.ClearFightSpot(Tank);
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
    tank.group.ClearFightSpot(Tank);
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
    tank.group.ClearFightSpot(Tank);
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

// jvbastel/WOWAI#127: the pull target that attacks the tank holds the
// pull, but the damage bots wait for the melee contact (DamageRules.h).
TEST(WarriorTank, HoldsThePullWhenThePullTargetAttacksIt)
{
    WarriorTank tank;
    tank.group.StartPull(Tank, Kobold);
    tank.fake.SetVictim(Kobold, Tank);

    tank.Think();

    Bots::PullState const pull = tank.group.GetPull();
    EXPECT_EQ(Bots::PullPhase::Holding, pull.phase);
    EXPECT_FALSE(tank.group.TankHoldsThreat(pull.engagedMs));
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

namespace
{
    // A bow tank at (0, 0, 0) that walked along the x axis from x = -30,
    // with the kobold 25 yards ahead in a pack of its own.
    struct TrailTank : BowTank
    {
        TrailTank()
        {
            for (float x = -30.0f; x <= 0.0f; x += 2.0f)
                group.KeepTrail({x, 0, 0}, false);
            AddEnemy(Kobold, {25, 0, 0}, 10.0f);
            fake.SetDistance(Kobold, 25.0f);
        }

        void AddEnemy(Bots::Guid enemy, Bots::Position at, float aggroYards)
        {
            fake.SetPosition(enemy, at);
            fake.AddEnemy(enemy, at, aggroYards);
        }
    };
}

// jvbastel/WOWAI#105: the tank holds the pack on its cleared way, where
// no other pack joins.
TEST(WarriorTank, HoldsThePullAtTheFightSpotBackOnItsTrail)
{
    TrailTank tank;
    // The wolf's reach (aggro 10 and the margin) ends between x = -6 and -8.
    tank.AddEnemy(Wolf, {5, 8, 0}, 10.0f);
    tank.group.StartPull(Tank, Kobold);

    tank.Think();

    Bots::PullState const pull = tank.group.GetPull();
    ASSERT_TRUE(pull.hasFightSpot);
    EXPECT_EQ(pull.fightSpot.x, -8.0f);
    EXPECT_FALSE(pull.hasShootSpot);
    EXPECT_TRUE(tank.fake.Deeds().empty());
}

TEST(WarriorTank, WarnsInPartyChatWhenThePullBringsAnotherPackAnyway)
{
    TrailTank tank;
    // Within the assistance radius of the kobold: it joins wherever the
    // tank fights.
    tank.AddEnemy(Wolf, {25, 9.5f, 0}, 10.0f);
    tank.group.StartPull(Tank, Kobold);

    tank.Think();

    EXPECT_TRUE(tank.group.GetPull().hasFightSpot);
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"warn Careful: this pull brings 1 more pack."}));
    tank.fake.ClearDeeds();
    tank.Think();
    for (std::string const& deed : tank.fake.Deeds())
        EXPECT_NE(deed, "warn Careful: this pull brings 1 more pack.");
}

// The review of jvbastel/WOWAI#100: no spot passes only for lack of a shot.
// The tank pulls anyway, and warns once.
TEST(WarriorTank, WarnsWhenNoSpotHasAShot)
{
    TrailTank tank;
    tank.fake.AddBlindToFromX(Kobold, -1000.0f);
    tank.fake.AddNoPathFromX(-1000.0f);
    tank.group.StartPull(Tank, Kobold);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(),
        (std::vector<std::string>{"warn Careful: I cannot pull this pack from a safe spot; more packs may come."}));
    tank.fake.ClearDeeds();
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000022 to 25 yards"}));
}

// A clean pull whose target walked out of shot (during the group wait): the
// chase into range is no failed fight spot, so no warn.
TEST(WarriorTank, NoWarnWhenACleanPullsTargetWalksOutOfShot)
{
    BowTank tank;
    tank.fake.AddNoPathFromX(-1000.0f);
    tank.Pull();
    tank.fake.SetDistance(Kobold, 45.0f);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000022 to 25 yards"}));
}

TEST(WarriorTank, WalksBackAlongItsTrailToAShootSpotWhenItCannotSeeThePullTarget)
{
    TrailTank tank;
    tank.fake.AddBlindToFromX(Kobold, -3.0f);
    tank.Pull();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"keep move to -4 0 0"}));

    // There, it sees the kobold and shoots.
    tank.fake.SetPosition(Tank, {-4, 0, 0});
    tank.fake.ClearDeeds();
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"stand", CastOn(ShootBow, Kobold)}));
}

// The review of jvbastel/WOWAI#100: a tank within the least shot range of
// the target does not run in when its fight spot search found a shoot spot
// on the trail: it walks there and shoots.
TEST(WarriorTank, TooNearToShootItWalksToItsShootSpotOnTheTrail)
{
    TrailTank tank;
    tank.fake.SetPosition(Tank, {19, 0, 0});
    tank.fake.SetDistance(Kobold, 6.0f);
    tank.Pull();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"keep move to 0 0 0"}));
}

// The shoot spot was wrong (the sight of the real game differs): the tank
// pulls as without one.
TEST(WarriorTank, AtItsShootSpotOutOfSightItPullsAsWithoutOne)
{
    TrailTank tank;
    tank.fake.AddBlindToFromX(Kobold, -3.0f);
    tank.Pull();
    tank.fake.SetPosition(Tank, {-4, 0, 0});
    tank.fake.AddBlindToFromX(Kobold, -1000.0f);

    tank.Think();
    tank.Think();

    // Its chase to the target is no checked way: it warns, once.
    EXPECT_FALSE(tank.group.GetPull().hasShootSpot);
    EXPECT_EQ(tank.fake.Deeds(),
        (std::vector<std::string>{"warn Careful: I cannot pull this pack from a safe spot; more packs may come.",
            "chase 0xf130000302000022 to 25 yards"}));
    tank.fake.ClearDeeds();
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"chase 0xf130000302000022 to 25 yards"}));
}

// jvbastel/WOWAI#106: the tank pulls when each bot of the group is at its
// member spot, or after 10 seconds.
namespace
{
    struct GroupTank : TrailTank
    {
        GroupTank()
        {
            fake.SetHealth(Healer, 600, 600);
            fake.SetGroupMembers({Healer});
            group.NoteBot(Tank, 1000, true);
            group.NoteBot(Healer, 1000, true);
            group.StartPull(Tank, Kobold);
            nowMs = 1000;
            Think();
            fake.ClearDeeds();
        }
    };
}

TEST(WarriorTank, WaitsForTheGroupBeforeItShoots)
{
    GroupTank tank;
    tank.nowMs = 1500;

    EXPECT_EQ(tank.Think(), "wait for the group");

    EXPECT_TRUE(tank.fake.Deeds().empty());
}

TEST(WarriorTank, ShootsOnceEachBotIsInPlace)
{
    GroupTank tank;
    tank.group.ClaimMemberSpot(Healer, Bots::Tests::SpotAt(true, {-16, 0, 0}));
    tank.group.SetMemberInPlace(Healer);
    tank.nowMs = 1500;

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"stand", CastOn(ShootBow, Kobold)}));
}

TEST(WarriorTank, SaysInPartyChatThatItWaitsOnABotAndShootsAfterTenSeconds)
{
    GroupTank tank;

    tank.nowMs = 1000 + Bots::GroupWaitSayMs;
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"say Waiting for 1 member to get in place."}));
    tank.fake.ClearDeeds();
    tank.nowMs += 500;
    tank.Think();
    EXPECT_TRUE(tank.fake.Deeds().empty());

    // At 10 s it pulls without the bot, and says so.
    tank.nowMs = 1000 + Bots::GroupWaitMs;
    tank.group.NoteBot(Healer, tank.nowMs, true);
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"say Pulling without 1 member."}));
    tank.fake.ClearDeeds();
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"stand", CastOn(ShootBow, Kobold)}));
}

TEST(WarriorTank, DoesNotWaitForThePlayer)
{
    GroupTank tank;
    Bots::Guid const Player = 0x05;
    tank.fake.SetHealth(Player, 600, 600);
    // The player is a member, but no bot: the coordinator does not know it.
    tank.fake.SetGroupMembers({Player});
    tank.nowMs = 1500;

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"stand", CastOn(ShootBow, Kobold)}));
}

TEST(WarriorTank, WalksToItsShootSpotWhileTheGroupGetsInPlace)
{
    TrailTank tank;
    tank.fake.SetHealth(Healer, 600, 600);
    tank.fake.SetGroupMembers({Healer});
    tank.group.NoteBot(Healer, 1000, true);
    tank.fake.AddBlindToFromX(Kobold, -3.0f);
    tank.group.StartPull(Tank, Kobold);
    tank.Think();
    tank.fake.ClearDeeds();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"keep move to -4 0 0"}));
}

// jvbastel/WOWAI#111: the healer waits from the engage of the tank.
TEST(WarriorTank, NotesWhenItEngages)
{
    WarriorTank tank;
    tank.group.StartPull(Tank, Kobold);
    tank.fake.SetVictim(Kobold, Tank);
    tank.nowMs = 7000;

    tank.Think();

    EXPECT_EQ(tank.group.GetPull().engagedMs, 7000);
}

// jvbastel/WOWAI#116: Shield Block.
namespace
{
    // A tank in Defensive Stance with a shield, 20 rage, Shield Block
    // known, and the kobold hitting it in melee. Revenge is not ready.
    struct ShieldTank : WarriorTank
    {
        ShieldTank()
        {
            fake.SetKnownSpell(Warrior::ShieldBlock);
            InFightWithKobold();
            fake.SetPower(Tank, 20, 100);
            fake.SetCannotCast(Warrior::Revenge);
        }
    };
}

TEST(WarriorTank, UsesShieldBlockWhenAnEnemyHitsItInMelee)
{
    ShieldTank tank;

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::ShieldBlock, Tank)}));
}

TEST(WarriorTank, NoShieldBlockOutsideDefensiveStance)
{
    ShieldTank tank;
    tank.fake.SetAura(Tank, Warrior::DefensiveStance, false);
    tank.fake.SetAura(Tank, Warrior::BattleStance);
    // The stance change cannot come now (its cooldown).
    tank.fake.SetCannotCast(Warrior::DefensiveStance);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));
}

TEST(WarriorTank, NoShieldBlockBelowTenRage)
{
    ShieldTank tank;
    tank.fake.SetPower(Tank, 9, 100);
    // Bloodrage is on its cooldown.
    tank.fake.SetCannotCast(Warrior::Bloodrage);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));
}

TEST(WarriorTank, UsesShieldBlockAtTenRage)
{
    ShieldTank tank;
    tank.fake.SetPower(Tank, 10, 100);
    tank.fake.SetCannotCast(Warrior::Bloodrage);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::ShieldBlock, Tank)}));
}

TEST(WarriorTank, NoShieldBlockWhenNoEnemyHitsIt)
{
    ShieldTank tank;
    // The kobold is stunned and hits nobody.
    tank.fake.SetVictim(Kobold, NoGuid);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));
}

TEST(WarriorTank, NoShieldBlockWhenAnEnemyHitsItOnlyFromRange)
{
    ShieldTank tank;
    tank.fake.SetVictim(Kobold, NoGuid);
    // An enemy hits the tank from 30 yards (out of melee reach): a shield cannot block it.
    tank.fake.SetHealth(Wolf, 200, 200);
    tank.fake.SetDistance(Wolf, 30.0f);
    tank.fake.SetVictim(Wolf, Tank);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));
}

TEST(WarriorTank, UsesShieldBlockWhenAnotherEnemyHitsItInMelee)
{
    ShieldTank tank;
    tank.fake.SetVictim(Kobold, NoGuid);
    tank.fake.SetHealth(Wolf, 200, 200);
    tank.fake.SetDistance(Wolf, 2.0f);
    tank.fake.SetVictim(Wolf, Tank);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::ShieldBlock, Tank)}));
}

// The cast check of the game fails with no shield equipped.
TEST(WarriorTank, NoShieldBlockWithNoShield)
{
    ShieldTank tank;
    tank.fake.SetCannotCast(Warrior::ShieldBlock);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));
}

// The cast check of the game fails on the cooldown too.
TEST(WarriorTank, NoShieldBlockOnItsCooldown)
{
    ShieldTank tank;
    tank.fake.SetCannotCastOn(Warrior::ShieldBlock, Tank);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));
}

TEST(WarriorTank, NoShieldBlockBeforeItKnowsIt)
{
    ShieldTank tank;
    tank.fake.SetKnownSpell(Warrior::ShieldBlock, false);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));
}

TEST(WarriorTank, PrefersReadyRevengeToShieldBlock)
{
    // As ShieldTank, but Revenge is ready.
    WarriorTank tank;
    tank.fake.SetKnownSpell(Warrior::ShieldBlock);
    tank.InFightWithKobold();
    tank.fake.SetPower(Tank, 20, 100);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Revenge, Kobold)}));
}

TEST(WarriorTank, PrefersShieldBlockToHeroicStrike)
{
    ShieldTank tank;
    tank.fake.SetPower(Tank, 60, 100);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::ShieldBlock, Tank)}));
}

TEST(WarriorTank, PrefersShieldBlockToSunderArmor)
{
    ShieldTank tank;
    // Sunder Armor is ready and its rage is there: with Shield Block on
    // its cooldown, the tank sunders.
    tank.fake.SetCannotCast(Warrior::ShieldBlock);
    tank.Think();
    ASSERT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));

    ShieldTank ready;

    ready.Think();

    EXPECT_EQ(ready.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::ShieldBlock, Tank)}));
}

// jvbastel/WOWAI#114: the opener and Demoralizing Shout.
namespace
{
    // A tank that knows Demoralizing Shout, in a fight with the kobold and
    // the wolf. Both stand next to it and attack it.
    struct ShoutTank : WarriorTank
    {
        // Two more enemies, out of the fight unless a test says so.
        static Bots::Guid const Boar = 0xF130000302000066;
        static Bots::Guid const Raptor = 0xF130000302000077;

        ShoutTank()
        {
            fake.SetKnownSpell(Warrior::DemoralizingShout);
            InFightWithKobold();
            fake.SetPosition(Tank, {0, 0, 0});
            fake.AddEnemy(Kobold, {2, 0, 0}, 10.0f);
            fake.AddEnemy(Wolf, {0, 3, 0}, 10.0f);
            fake.SetHealth(Wolf, 200, 200);
            fake.SetVictim(Wolf, Tank);
            // Rage for each action but Bloodrage and Heroic Strike.
            fake.SetPower(Tank, 25, 100);
        }

        // The think of one deed, which then has its effect on the fake.
        std::string Next()
        {
            fake.ClearDeeds();
            Think();
            std::vector<std::string> const deeds = fake.Deeds();
            return deeds.empty() ? "" : deeds.back();
        }
    };
}

// The tank charged the kobold in Battle Stance (Charge gave it rage). It
// takes Defensive Stance at once, which drops all its rage with no Tactical
// Mastery. Then Bloodrage gives it rage, and with that rage it shouts.
TEST(WarriorTank, AfterChargeTakesDefensiveStanceThenBloodrageThenDemoralizingShout)
{
    ShoutTank tank;
    tank.fake.SetAura(Tank, Warrior::DefensiveStance, false);
    tank.fake.SetAura(Tank, Warrior::BattleStance);
    tank.fake.SetPower(Tank, 9, 100);

    EXPECT_EQ(tank.Next(), CastOn(Warrior::DefensiveStance, Tank));
    tank.fake.SetAura(Tank, Warrior::BattleStance, false);
    tank.fake.SetAura(Tank, Warrior::DefensiveStance);
    tank.fake.SetPower(Tank, 0, 100);
    // Demoralizing Shout costs 10 rage: the game refuses it with less.
    tank.fake.SetCannotCastOn(Warrior::DemoralizingShout, Tank);

    EXPECT_EQ(tank.Next(), CastOn(Warrior::Bloodrage, Tank));
    tank.fake.SetPower(Tank, 10, 100);
    tank.fake.SetCanCastOn(Warrior::DemoralizingShout, Tank);
    tank.fake.SetCannotCast(Warrior::Bloodrage);

    EXPECT_EQ(tank.Next(), CastOn(Warrior::DemoralizingShout, Tank));
}

TEST(WarriorTank, WithoutChargeOpensWithBloodrageThenDemoralizingShout)
{
    ShoutTank tank;
    tank.fake.SetPower(Tank, 0, 100);
    tank.fake.SetCannotCastOn(Warrior::DemoralizingShout, Tank);

    EXPECT_EQ(tank.Next(), CastOn(Warrior::Bloodrage, Tank));
    tank.fake.SetPower(Tank, 10, 100);
    tank.fake.SetCanCastOn(Warrior::DemoralizingShout, Tank);
    tank.fake.SetCannotCast(Warrior::Bloodrage);

    EXPECT_EQ(tank.Next(), CastOn(Warrior::DemoralizingShout, Tank));
}

TEST(WarriorTank, ShoutsWhenTwoEnemiesNearLackTheDebuff)
{
    ShoutTank tank;
    tank.fake.SetAura(Kobold, Warrior::DemoralizingShout);
    tank.fake.SetAura(Wolf, Warrior::DemoralizingShout);
    tank.fake.AddEnemy(ShoutTank::Boar, {5, 0, 0}, 10.0f);
    tank.fake.AddEnemy(ShoutTank::Raptor, {0, 5, 0}, 10.0f);

    EXPECT_EQ(tank.Next(), CastOn(Warrior::DemoralizingShout, Tank));
}

// The wolf came late, or its debuff ran out.
TEST(WarriorTank, ShoutsWhenOneEnemyInTheFightLacksTheDebuff)
{
    ShoutTank tank;
    tank.fake.SetAura(Kobold, Warrior::DemoralizingShout);

    EXPECT_EQ(tank.Next(), CastOn(Warrior::DemoralizingShout, Tank));
}

TEST(WarriorTank, NoShoutWhenEachEnemyNearHasTheDebuff)
{
    ShoutTank tank;
    tank.fake.SetAura(Kobold, Warrior::DemoralizingShout);
    tank.fake.SetAura(Wolf, Warrior::DemoralizingShout);

    EXPECT_EQ(tank.Next(), CastOn(Warrior::Revenge, Kobold));
}

TEST(WarriorTank, NoShoutForOneEnemyOutOfTheFight)
{
    ShoutTank tank;
    tank.fake.SetAura(Kobold, Warrior::DemoralizingShout);
    tank.fake.SetAura(Wolf, Warrior::DemoralizingShout);
    tank.fake.AddEnemy(ShoutTank::Boar, {5, 0, 0}, 10.0f);

    EXPECT_EQ(tank.Next(), CastOn(Warrior::Revenge, Kobold));
}

TEST(WarriorTank, NoShoutForEnemiesBeyondItsReach)
{
    ShoutTank tank;
    tank.fake.SetAura(Kobold, Warrior::DemoralizingShout);
    tank.fake.SetAura(Wolf, Warrior::DemoralizingShout);
    // An aggro distance of 5: with 10, the boar out of the fight would
    // make the tank step out of its reach first (KeepOut.h, #121).
    tank.fake.AddEnemy(ShoutTank::Boar, {11, 0, 0}, 5.0f);
    tank.fake.AddEnemy(ShoutTank::Raptor, {0, 11, 0}, 10.0f);
    tank.fake.SetDistance(ShoutTank::Raptor, 11.0f);
    tank.fake.SetVictim(ShoutTank::Raptor, Tank);

    EXPECT_EQ(tank.Next(), CastOn(Warrior::Revenge, Kobold));
}

// Below level 14 the tank knows no Demoralizing Shout.
TEST(WarriorTank, WithoutDemoralizingShoutItStillPicksItsOtherActions)
{
    ShoutTank tank;
    tank.fake.SetKnownSpell(Warrior::DemoralizingShout, false);
    tank.fake.SetPower(Tank, 0, 100);

    EXPECT_EQ(tank.Next(), CastOn(Warrior::Bloodrage, Tank));
    tank.fake.SetPower(Tank, 25, 100);

    EXPECT_EQ(tank.Next(), CastOn(Warrior::Revenge, Kobold));
}

// With no Tactical Mastery (before level 51), their stance change would drop
// all its rage.
TEST(WarriorTank, NeverCastsThunderClapOrMockingBlow)
{
    ShoutTank tank;
    tank.fake.SetKnownSpell(Warrior::ThunderClap);
    tank.fake.SetKnownSpell(Warrior::MockingBlow);
    tank.fake.SetAura(Tank, Warrior::DefensiveStance, false);
    tank.fake.SetAura(Tank, Warrior::BattleStance);
    tank.fake.SetPower(Tank, 60, 100);
    tank.fake.SetHealth(Healer, 300, 300);
    tank.fake.SetGroupMembers({Healer});
    tank.fake.SetVictim(Kobold, Healer);
    tank.fake.SetCannotCast(Warrior::Taunt);

    std::vector<std::string> deeds;
    for (int think = 0; think < 8; ++think)
    {
        deeds.push_back(tank.Next());
        tank.fake.SetCannotCast(Warrior::DefensiveStance);
        tank.fake.SetCannotCast(Warrior::DemoralizingShout);
        tank.fake.SetCannotCast(Warrior::Revenge);
        tank.fake.SetCannotCast(Warrior::HeroicStrike);
    }

    for (std::string const& deed : deeds)
    {
        EXPECT_EQ(std::string::npos, deed.find("cast " + std::to_string(Warrior::ThunderClap) + " ")) << deed;
        EXPECT_EQ(std::string::npos, deed.find("cast " + std::to_string(Warrior::MockingBlow) + " ")) << deed;
    }
}

// Out of combat (no fight with the focus) the shout would pull the enemies
// near.
TEST(WarriorTank, NoShoutOutOfCombat)
{
    ShoutTank tank;
    tank.fake.SetInCombat(Tank, false);
    tank.fake.AddEnemy(ShoutTank::Boar, {5, 0, 0}, 10.0f);
    tank.fake.AddEnemy(ShoutTank::Raptor, {0, 5, 0}, 10.0f);

    EXPECT_NE(tank.Next(), CastOn(Warrior::DemoralizingShout, Tank));
}

// jvbastel/WOWAI#122: a pull target on a ledge that no safe place sees
// (two_packs, pack48). The tank warns, walks up its path to the first point
// with a shot, and shoots from there.
TEST(WarriorTank, ClosesInAlongItsPathToAShotAtATargetOnALedge)
{
    BowTank tank;
    tank.fake.SetPosition(Kobold, {25, 0, 0});
    // Every place round the kobold is within its aggro distance and margin.
    tank.fake.AddEnemy(Kobold, {25, 0, 0}, 25.0f);
    tank.fake.AddBlindToBelowX(Kobold, 15.0f);
    tank.group.StartPull(Tank, Kobold);
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(),
        (std::vector<std::string>{"warn Careful: I cannot pull this pack from a safe spot; more packs may come."}));
    tank.fake.ClearDeeds();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"keep move to 15 0 0"}));

    // There, it sees the kobold and shoots.
    tank.fake.SetPosition(Tank, {15, 0, 0});
    tank.fake.SetDistance(Kobold, 10.0f);
    tank.fake.ClearDeeds();
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{"stand", CastOn(ShootBow, Kobold)}));
}

// jvbastel/WOWAI#115: healer-first Taunt and Challenging Shout.
namespace
{
    Bots::Guid const Rogue = 0x0000000000000013;
    Bots::Guid const Bat = 0xF130000302000044;
    Bots::Guid const Rat = 0xF130000302000055;

    // The tank holds the kobold, and the group has a rogue and a healer
    // (the rogue first in the member order).
    struct GroupInFight : WarriorTank
    {
        GroupInFight()
        {
            fake.SetGroupMembers({Rogue, Healer});
            group.SetHealer(Healer);
            InFightWithKobold();
            fake.SetInCombat(Tank);
            for (Bots::Guid unit : {Rogue, Healer, Wolf, Bat, Rat})
                fake.SetHealth(unit, 300, 300);
        }
    };
}

TEST(WarriorTank, TauntsTheEnemyOnTheHealerFirst)
{
    GroupInFight tank;
    tank.fake.SetVictim(Wolf, Rogue);
    tank.fake.SetVictim(Bat, Healer);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Taunt, Bat)}));
}

TEST(WarriorTank, TauntsTheFocusBeforeTheEnemyOnTheHealer)
{
    GroupInFight tank;
    tank.fake.SetVictim(Kobold, Rogue);
    tank.fake.SetVictim(Bat, Healer);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Taunt, Kobold)}));
}

TEST(WarriorTank, ShoutsAtThreeLooseEnemies)
{
    GroupInFight tank;
    tank.fake.SetKnownSpell(Warrior::ChallengingShout);
    tank.fake.SetCannotCast(Warrior::Taunt);
    tank.fake.SetVictim(Wolf, Rogue);
    tank.fake.SetVictim(Bat, Healer);
    tank.fake.SetVictim(Rat, Healer);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::ChallengingShout, Tank)}));
}

TEST(WarriorTank, DoesNotShoutAtTwoLooseEnemies)
{
    GroupInFight tank;
    tank.fake.SetKnownSpell(Warrior::ChallengingShout);
    tank.fake.SetCannotCast(Warrior::Taunt);
    tank.fake.SetVictim(Wolf, Rogue);
    tank.fake.SetVictim(Bat, Healer);

    tank.Think();

    for (std::string const& deed : tank.fake.Deeds())
        EXPECT_NE(deed, CastOn(Warrior::ChallengingShout, Tank));
}

// Below level 26 the tank knows no Challenging Shout: it taunts the loose
// enemies one by one, the one on the healer first.
TEST(WarriorTank, WithoutChallengingShoutTauntsLooseEnemiesOneByOne)
{
    GroupInFight tank;
    tank.fake.SetVictim(Wolf, Rogue);
    tank.fake.SetVictim(Bat, Healer);
    tank.fake.SetVictim(Rat, Rogue);

    tank.Think();
    tank.fake.SetVictim(Bat, Tank);
    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(),
        (std::vector<std::string>{CastOn(Warrior::Taunt, Bat), CastOn(Warrior::Taunt, Wolf)}));
}

TEST(WarriorTank, DoesNotShoutWhenALooseEnemyIsOutOfShoutRange)
{
    GroupInFight tank;
    tank.fake.SetKnownSpell(Warrior::ChallengingShout);
    tank.fake.SetCannotCast(Warrior::Taunt);
    tank.fake.SetVictim(Wolf, Rogue);
    tank.fake.SetVictim(Bat, Healer);
    tank.fake.SetVictim(Rat, Healer);
    tank.fake.SetDistance(Rat, 15.0f);

    tank.Think();

    for (std::string const& deed : tank.fake.Deeds())
        EXPECT_NE(deed, CastOn(Warrior::ChallengingShout, Tank));
}

// jvbastel/WOWAI#117: the threat target.
namespace
{
    Bots::Guid const Lynx = 0xF130000302000088;

    // The tank fights the kobold (the focus), and the wolf and the lynx
    // attack it too. The healer stands back. Revenge is not ready, unless
    // `revengeReady`.
    struct ThreeEnemyTank : WarriorTank
    {
        explicit ThreeEnemyTank(bool revengeReady = false)
        {
            fake.SetGroupMembers({Healer});
            fake.SetHealth(Healer, 300, 300);
            group.SetHealer(Healer);
            InFightWithKobold();
            fake.SetInCombat(Tank);
            fake.SetPower(Tank, 30, 100);
            if (!revengeReady)
                fake.SetCannotCast(Warrior::Revenge);
            for (Bots::Guid enemy : {Wolf, Lynx})
            {
                fake.SetHealth(enemy, 300, 300);
                fake.SetVictim(enemy, Tank);
            }
        }

        void SetRatios(float kobold, float wolf, float lynx)
        {
            fake.SetThreatRatio(Tank, Kobold, kobold);
            fake.SetThreatRatio(Tank, Wolf, wolf);
            fake.SetThreatRatio(Tank, Lynx, lynx);
        }
    };
}

TEST(WarriorTank, StaysOnTheFocusBelowAThreatLeadOf150Percent)
{
    ThreeEnemyTank tank;
    tank.SetRatios(1.49f, 1.0f, 1.0f);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));
}

TEST(WarriorTank, AtA150PercentLeadSundersTheEnemyWithTheLowestThreatRatio)
{
    ThreeEnemyTank tank;
    tank.SetRatios(1.5f, 2.0f, 1.2f);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Lynx)}));
}

TEST(WarriorTank, UsesRevengeOnTheThreatTargetWhenItIsReady)
{
    ThreeEnemyTank tank(true);
    tank.SetRatios(3.0f, 1.1f, 2.0f);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Revenge, Wolf)}));
}

TEST(WarriorTank, BreaksATieOnTheThreatRatioByTheDistanceToTheHealer)
{
    ThreeEnemyTank tank;
    tank.SetRatios(2.0f, 1.0f, 1.0f);
    tank.fake.SetDistanceBetween(Wolf, Healer, 15.0f);
    tank.fake.SetDistanceBetween(Lynx, Healer, 8.0f);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Lynx)}));
}

TEST(WarriorTank, GoesBackToTheFocusAfterOneAbilityOnTheThreatTarget)
{
    ThreeEnemyTank tank;
    tank.SetRatios(2.0f, 1.0f, 1.5f);
    tank.Think();
    tank.fake.ClearDeeds();

    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));
    tank.fake.ClearDeeds();

    // The next visit, once it hit the focus.
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Wolf)}));
}

TEST(WarriorTank, TheThreatTargetLeavesTheGroupsFocusAlone)
{
    ThreeEnemyTank tank;
    tank.SetRatios(2.0f, 1.0f, 1.5f);

    tank.Think();

    ASSERT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Wolf)}));
    EXPECT_EQ(tank.group.GetFocusTarget(), Kobold);
}

TEST(WarriorTank, OnAVisitPrefersReadyRevengeOnTheThreatTargetToShieldBlock)
{
    ThreeEnemyTank tank(true);
    tank.fake.SetKnownSpell(Warrior::ShieldBlock);
    tank.SetRatios(3.0f, 1.1f, 2.0f);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Revenge, Wolf)}));
}

TEST(WarriorTank, AfterRevengeOnTheThreatTargetGoesBackToTheFocus)
{
    ThreeEnemyTank tank(true);
    tank.SetRatios(3.0f, 1.1f, 2.0f);
    tank.Think();
    tank.fake.ClearDeeds();

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Revenge, Kobold)}));
}

TEST(WarriorTank, NoThreatTargetWhenNoOtherEnemyIsInReach)
{
    ThreeEnemyTank tank;
    tank.SetRatios(2.0f, 1.0f, 1.0f);
    tank.fake.SetCannotCastOn(Warrior::SunderArmor, Wolf);
    tank.fake.SetCannotCastOn(Warrior::SunderArmor, Lynx);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));
}

TEST(WarriorTank, IgnoresADeadHealerInTheTieBreak)
{
    ThreeEnemyTank tank;
    tank.SetRatios(2.0f, 1.0f, 1.0f);
    tank.fake.SetDistanceBetween(Wolf, Healer, 8.0f);
    tank.fake.SetDistanceBetween(Lynx, Healer, 15.0f);
    tank.fake.SetHealth(Healer, 0, 300);
    tank.Think();
    std::vector<std::string> const deadHealer = tank.fake.Deeds();

    ThreeEnemyTank swapped;
    swapped.SetRatios(2.0f, 1.0f, 1.0f);
    swapped.fake.SetDistanceBetween(Wolf, Healer, 15.0f);
    swapped.fake.SetDistanceBetween(Lynx, Healer, 8.0f);
    swapped.fake.SetHealth(Healer, 0, 300);
    swapped.Think();

    // The distances to the corpse do not change the pick.
    EXPECT_EQ(swapped.fake.Deeds(), deadHealer);
}

// jvbastel/WOWAI#102 review: the opener on the walk back, the visits.
namespace
{
    // After a ranged pull the kobold hits the tank, which walks back from
    // (28, 20) to its fight spot at (10, 20). It is in Battle Stance with
    // no rage, and knows Demoralizing Shout.
    struct WalkBackTank : BowTank
    {
        WalkBackTank()
        {
            fake.SetKnownSpell(Warrior::DemoralizingShout);
            fake.SetPosition(Tank, {10, 20, 0});
            Pull();
            fake.SetPosition(Tank, {28, 20, 0});
            fake.AddEnemy(Kobold, {29, 20, 0}, 10.0f);
            fake.SetDistance(Kobold, 2.0f);
            fake.SetInCombat(Tank);
            fake.SetVictim(Tank, Kobold);
            fake.SetVictim(Kobold, Tank);
            Think(); // Holds the pull.
            fake.ClearDeeds();
        }

        std::vector<std::string> Next()
        {
            fake.ClearDeeds();
            Think();
            return fake.Deeds();
        }
    };
}

// Spec story 3: with no Charge, the tank opens with Defensive Stance,
// Bloodrage and Demoralizing Shout, and walks on meanwhile.
TEST(WarriorTank, OpensWhileItWalksBackAfterARangedPull)
{
    WalkBackTank tank;
    std::string const walk = "keep move to 10 20 0";

    EXPECT_EQ(tank.Next(), (std::vector<std::string>{CastOn(Warrior::DefensiveStance, Tank), walk}));
    tank.fake.SetAura(Tank, Warrior::BattleStance, false);
    tank.fake.SetAura(Tank, Warrior::DefensiveStance);
    tank.fake.SetCannotCastOn(Warrior::DemoralizingShout, Tank);

    EXPECT_EQ(tank.Next(), (std::vector<std::string>{CastOn(Warrior::Bloodrage, Tank), walk}));
    tank.fake.SetPower(Tank, 10, 100);
    tank.fake.SetCanCastOn(Warrior::DemoralizingShout, Tank);
    tank.fake.SetCannotCast(Warrior::Bloodrage);

    EXPECT_EQ(tank.Next(), (std::vector<std::string>{CastOn(Warrior::DemoralizingShout, Tank), walk}));
    tank.fake.SetAura(Kobold, Warrior::DemoralizingShout);

    EXPECT_EQ(tank.Next(), (std::vector<std::string>{walk}));
}

// The end of a fight ends a visit too: the next fight starts with a full one.
TEST(WarriorTank, ANewFightStartsWithANewVisit)
{
    ThreeEnemyTank tank;
    tank.SetRatios(2.0f, 1.0f, 1.5f);
    tank.Think();
    ASSERT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Wolf)}));
    tank.fake.SetHealth(Kobold, 0, 200);
    ASSERT_EQ(tank.Think(), "end the fight");

    tank.group.SetFocusTarget(Lynx);
    tank.fake.SetVictim(Tank, Lynx);
    tank.fake.ClearDeeds();
    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Wolf)}));
}

// A fight can end on other paths than "end the fight" (a wipe, the Stop
// command, the tank's death): a new focus starts with a new visit too.
TEST(WarriorTank, ANewFocusStartsWithANewVisit)
{
    ThreeEnemyTank tank;
    tank.SetRatios(2.0f, 1.0f, 1.5f);
    tank.Think();
    ASSERT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Wolf)}));

    tank.group.EndPull();
    tank.group.SetFocusTarget(Lynx);
    tank.fake.SetVictim(Tank, Lynx);
    tank.fake.ClearDeeds();
    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Wolf)}));
}

// Spec story 11: the lead on the focus drops below 150% in a visit, before
// its ability: the next action goes to the focus.
TEST(WarriorTank, GoesBackToTheFocusWhenItsLeadDropsInAVisit)
{
    ThreeEnemyTank tank;
    tank.SetRatios(2.0f, 1.0f, 1.5f);
    // The visit is due, but Shield Block (above it) takes this think.
    tank.fake.SetKnownSpell(Warrior::ShieldBlock);
    tank.Think();
    ASSERT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::ShieldBlock, Tank)}));

    tank.fake.SetCannotCast(Warrior::ShieldBlock);
    tank.fake.SetThreatRatio(Tank, Kobold, 1.4f);
    tank.fake.ClearDeeds();
    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));
}

// jvbastel/WOWAI#126: the tank picks up a loose enemy that Taunt cannot reach.
namespace
{
    std::string Attack(Bots::Guid target)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "attack 0x%016llx", static_cast<unsigned long long>(target));
        return text;
    }

    std::string Chase(Bots::Guid target)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "chase 0x%016llx", static_cast<unsigned long long>(target));
        return text;
    }

    // The bat hits the healer 28 yards from the tank, out of the 20 yards of
    // Taunt (pack 8 of runs/t99-1027).
    struct BatOnTheFarHealer : GroupInFight
    {
        BatOnTheFarHealer()
        {
            fake.SetSpellRange(Warrior::Taunt, 20.0f);
            // Melee reach (jvbastel/WOWAI#168: the abilities on the loose
            // enemy).
            fake.SetSpellRange(Warrior::SunderArmor, 5.0f);
            fake.SetSpellRange(Warrior::Revenge, 5.0f);
            fake.SetVictim(Bat, Healer);
            fake.SetDistance(Bat, 28.0f);
        }
    };
}

// A second add hits a member while Taunt waits for its cooldown (it went out
// on the first add). jvbastel/WOWAI#169: the member is stunned, so the tank
// goes to it.
TEST(WarriorTank, AttacksASecondAddOnAStunnedMemberWhenTauntIsOnItsCooldown)
{
    GroupInFight tank;
    tank.fake.SetRootedOrStunned(Rogue);
    tank.fake.SetCannotCast(Warrior::Taunt);
    tank.fake.SetSpellRange(Warrior::SunderArmor, 5.0f);
    tank.fake.SetSpellRange(Warrior::Revenge, 5.0f);
    tank.fake.SetVictim(Wolf, Tank);
    tank.fake.SetVictim(Bat, Rogue);
    tank.fake.SetDistance(Bat, 8.0f);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{Attack(Bat), Chase(Bat)}));
}

// After the taunt, the bat attacks the tank, but the healer's threat on it
// equals the tank's (a taunt gives the top threat, no lead): the tank
// sunders the bat until it leads on its threat, then goes back to the focus.
TEST(WarriorTank, AfterATauntKeepsHittingTheTauntedEnemyUntilItHoldsIt)
{
    GroupInFight tank;
    tank.fake.SetPower(Tank, 30, 100);
    tank.fake.SetCannotCast(Warrior::Revenge);
    tank.fake.SetVictim(Bat, Healer);
    tank.Think();
    ASSERT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Taunt, Bat)}));
    tank.fake.SetCannotCast(Warrior::Taunt);
    tank.fake.SetVictim(Bat, Tank);
    tank.fake.SetThreatRatio(Tank, Bat, 1.0f);

    tank.fake.ClearDeeds();
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Bat)}));

    tank.fake.SetThreatRatio(Tank, Bat, 1.5f);
    tank.fake.ClearDeeds();
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));
}

TEST(WarriorTank, KeepsThreatOnTheTauntedEnemyWithAReadyRevenge)
{
    GroupInFight tank;
    tank.fake.SetPower(Tank, 30, 100);
    tank.fake.SetVictim(Bat, Healer);
    tank.Think();
    tank.fake.SetCannotCast(Warrior::Taunt);
    tank.fake.SetVictim(Bat, Tank);
    tank.fake.SetThreatRatio(Tank, Bat, 1.0f);

    tank.fake.ClearDeeds();
    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Revenge, Bat)}));
}

// The picked-up enemy: the tank reached it and hits it. It sunders it, and
// does not turn back to the focus while the enemy still hits the healer.
TEST(WarriorTank, SundersThePickedUpEnemyUntilItHoldsIt)
{
    // jvbastel/WOWAI#169: the healer is stunned, so the tank goes to it.
    BatOnTheFarHealer tank;
    tank.fake.SetRootedOrStunned(Healer);
    tank.fake.SetPower(Tank, 30, 100);
    tank.fake.SetCannotCast(Warrior::Revenge);
    tank.Think();
    ASSERT_EQ(tank.fake.Deeds(), (std::vector<std::string>{Attack(Bat), Chase(Bat)}));
    tank.fake.SetVictim(Tank, Bat);
    tank.fake.SetDistance(Bat, 2.0f);
    tank.fake.SetCannotCast(Warrior::Taunt);

    tank.fake.ClearDeeds();
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Bat)}));

    // It attacks the tank, which leads on its threat now: back to the focus.
    tank.fake.SetVictim(Bat, Tank);
    tank.fake.SetThreatRatio(Tank, Bat, 1.5f);
    tank.fake.ClearDeeds();
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{Attack(Kobold), Chase(Kobold)}));
}

// jvbastel/WOWAI#126 review: the focus on a member comes before a loose add.
// Taunt waits for its cooldown, and the focus hits the rogue: the tank stays
// on the focus and does not go to the bat on the healer.
TEST(WarriorTank, StaysOnTheFocusOnAMemberBeforeItPicksUpALooseEnemy)
{
    GroupInFight tank;
    tank.fake.SetCannotCast(Warrior::Taunt);
    tank.fake.SetVictim(Kobold, Rogue);
    tank.fake.SetVictim(Bat, Healer);
    tank.fake.SetDistance(Bat, 8.0f);
    tank.fake.SetDistance(Kobold, 12.0f);

    tank.Think();
    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{Attack(Kobold), Chase(Kobold)}));
    for (std::string const& deed : tank.fake.Deeds())
    {
        EXPECT_NE(deed, Attack(Bat));
        EXPECT_NE(deed, Chase(Bat));
    }
}

// jvbastel/WOWAI#126 review: the tank does not run far from the fight to a
// loose enemy (more packs wait on the way). It stays on the focus.
TEST(WarriorTank, DoesNotPickUpALooseEnemyBeyondThePickUpReach)
{
    BatOnTheFarHealer tank;
    tank.fake.SetRootedOrStunned(Healer);
    tank.fake.SetDistance(Bat, 45.0f);

    tank.Think();

    for (std::string const& deed : tank.fake.Deeds())
    {
        EXPECT_NE(deed, Attack(Bat));
        EXPECT_NE(deed, Chase(Bat));
    }
}

// jvbastel/WOWAI#121 merge review: the keep-out rule (KeepOut.h) and the
// pick-up of #126. The bat on the healer stands in the reach of a rat that
// is in no fight. The tank does not go to it: the keep-out rule would step
// it out again, and it would pull the rat. It stays on the focus.
TEST(WarriorTank, DoesNotPickUpALooseEnemyInTheReachOfAnotherPack)
{
    BatOnTheFarHealer tank;
    tank.fake.SetRootedOrStunned(Healer);
    tank.fake.SetPosition(Tank, {0, 0, 0});
    tank.fake.SetPosition(Bat, {28, 0, 0});
    tank.fake.AddEnemy(Rat, {36, 0, 0}, 10.0f);

    tank.Think();

    for (std::string const& deed : tank.fake.Deeds())
    {
        EXPECT_NE(deed, Attack(Bat));
        EXPECT_NE(deed, Chase(Bat));
    }
}

// Review of jvbastel/WOWAI#126 and #121 (C1): an add on the healer near
// another pack. The healer stands in the reach of the rat too and finds no
// place to step out to: nobody but the damage bots would take the add. The
// tank picks it up anyway, and warns once.
namespace
{
    struct BatOnTheHealerNearARat : BatOnTheFarHealer
    {
        BatOnTheHealerNearARat()
        {
            group.SetTank(Tank);
            fake.SetPosition(Tank, {0, 0, 0});
            // 10 yd from the rat: out of its aggro distance, in its margin.
            fake.SetPosition(Healer, {26, 0, 0});
            fake.SetPosition(Bat, {26, 0, 0});
            fake.SetInCombat(Healer);
            fake.AddEnemy(Rat, {36, 0, 0}, 10.0f);
        }

        int Warns() const
        {
            return int(std::count(fake.Deeds().begin(), fake.Deeds().end(),
                "warn Careful: this pull brings 1 more pack."));
        }
    };
}

TEST(WarriorTank, PicksUpTheHealersAddNearAnotherPackWhenTheHealerCannotStepOut)
{
    BatOnTheHealerNearARat tank;
    tank.fake.AddNoPathFromX(-10000.0f);

    tank.Think();

    EXPECT_EQ(tank.Warns(), 1);
    EXPECT_NE(std::find(tank.fake.Deeds().begin(), tank.fake.Deeds().end(), Attack(Bat)), tank.fake.Deeds().end());
    EXPECT_NE(std::find(tank.fake.Deeds().begin(), tank.fake.Deeds().end(), Chase(Bat)), tank.fake.Deeds().end());

    // On its way, the keep-out rule does not step it back, and it warns once.
    tank.fake.ClearDeeds();
    tank.fake.SetVictim(Tank, Bat);
    tank.fake.SetPosition(Tank, {22, 0, 0});
    tank.fake.SetDistance(Bat, 4.0f);
    EXPECT_NE(tank.Think(), Bots::KeepOutRule);
    EXPECT_EQ(tank.Warns(), 0);
}

TEST(WarriorTank, LeavesTheHealersAddNearAnotherPackWhenTheHealerCanStepOut)
{
    BatOnTheHealerNearARat tank;

    tank.Think();

    for (std::string const& deed : tank.fake.Deeds())
    {
        EXPECT_NE(deed, Attack(Bat));
        EXPECT_NE(deed, Chase(Bat));
    }
}

// C1: the pick-up tries each loose enemy, the healer's first: the add on
// the healer is in the rat's reach, so the tank picks up the add on the
// rogue.
TEST(WarriorTank, PicksUpTheNextLooseEnemyWhenTheFirstIsInTheReachOfAnotherPack)
{
    BatOnTheHealerNearARat tank;
    tank.fake.SetRootedOrStunned(Rogue);
    tank.fake.SetVictim(Wolf, Rogue);
    tank.fake.SetPosition(Wolf, {-25, 0, 0});
    tank.fake.SetDistance(Wolf, 25.0f);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{Attack(Wolf), Chase(Wolf)}));
}

// jvbastel/WOWAI#168: the tank keeps one loose enemy, and picks it by who it
// attacks: the healer, then damage bots in cloth, then the others; the
// nearest of a rank first.
namespace
{
    Bots::Guid const Mage = 0x0000000000000014;

    // GroupInFight with a mage (cloth) after the rogue, and melee reach on
    // Sunder Armor and Revenge. Taunt waits for its cooldown unless a test
    // makes it ready.
    struct LooseEnemiesTank : GroupInFight
    {
        LooseEnemiesTank()
        {
            fake.SetGroupMembers({Rogue, Mage, Healer});
            fake.SetHealth(Mage, 300, 300);
            fake.SetWearsCloth(Mage);
            fake.SetWearsCloth(Healer);
            fake.SetPower(Tank, 30, 100);
            fake.SetSpellRange(Warrior::SunderArmor, 5.0f);
            fake.SetSpellRange(Warrior::Revenge, 5.0f);
            fake.SetCannotCast(Warrior::Taunt);
            fake.SetCannotCast(Warrior::Revenge);
            for (Bots::Guid enemy : {Wolf, Bat, Rat})
                fake.SetDistance(enemy, 2.0f);
        }

        // The deeds of one think at `ms`.
        std::vector<std::string> ThinkAt(std::int64_t ms)
        {
            nowMs = ms;
            fake.ClearDeeds();
            Think();
            return fake.Deeds();
        }
    };
}

TEST(WarriorTankLooseEnemy, TakesTheEnemyOnAClothMemberBeforeOneOnAMemberInTougherArmour)
{
    LooseEnemiesTank tank;
    tank.fake.SetVictim(Wolf, Rogue);
    tank.fake.SetVictim(Bat, Mage);

    EXPECT_EQ(tank.ThinkAt(0), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Bat)}));
}

TEST(WarriorTankLooseEnemy, TakesTheEnemyOnTheHealerBeforeOneOnAClothMember)
{
    LooseEnemiesTank tank;
    tank.fake.SetVictim(Bat, Mage);
    tank.fake.SetVictim(Wolf, Healer);

    EXPECT_EQ(tank.ThinkAt(0), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Wolf)}));
}

TEST(WarriorTankLooseEnemy, TakesTheNearestOfTwoEnemiesOfOneRank)
{
    LooseEnemiesTank tank;
    tank.fake.SetVictim(Wolf, Rogue);
    tank.fake.SetVictim(Bat, Rogue);
    tank.fake.SetDistance(Wolf, 4.0f);
    tank.fake.SetDistance(Bat, 3.0f);

    EXPECT_EQ(tank.ThinkAt(0), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Bat)}));
}

// A loose enemy of a higher rank comes later: the tank stays on its enemy.
TEST(WarriorTankLooseEnemy, KeepsItsLooseEnemyAcrossThinks)
{
    LooseEnemiesTank tank;
    tank.fake.SetVictim(Wolf, Rogue);
    ASSERT_EQ(tank.ThinkAt(0), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Wolf)}));
    tank.fake.SetVictim(Bat, Healer);

    EXPECT_EQ(tank.ThinkAt(1500), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Wolf)}));
    EXPECT_EQ(tank.ThinkAt(3000), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Wolf)}));
}

TEST(WarriorTankLooseEnemy, GivesUpItsLooseEnemyAfterAbout4Seconds)
{
    LooseEnemiesTank tank;
    tank.fake.SetVictim(Wolf, Rogue);
    ASSERT_EQ(tank.ThinkAt(0), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Wolf)}));
    tank.fake.SetVictim(Bat, Rogue);
    tank.fake.SetDistance(Bat, 1.0f);

    EXPECT_EQ(tank.ThinkAt(4100), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Bat)}));
}

TEST(WarriorTankLooseEnemy, TakesTheNextLooseEnemyWhenItsEnemyAttacksTheTank)
{
    LooseEnemiesTank tank;
    tank.fake.SetVictim(Wolf, Rogue);
    ASSERT_EQ(tank.ThinkAt(0), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Wolf)}));
    tank.fake.SetVictim(Bat, Healer);
    tank.fake.SetVictim(Wolf, Tank);
    tank.fake.SetThreatRatio(Tank, Wolf, 2.0f);

    EXPECT_EQ(tank.ThinkAt(500), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Bat)}));
}

TEST(WarriorTankLooseEnemy, TauntsItsLooseEnemyWhenTauntIsReady)
{
    LooseEnemiesTank tank;
    tank.fake.SetVictim(Bat, Mage);
    tank.fake.SetCannotCast(Warrior::Taunt, false);
    tank.fake.SetCannotCast(Warrior::Revenge, false);

    EXPECT_EQ(tank.ThinkAt(0), (std::vector<std::string>{CastOn(Warrior::Taunt, Bat)}));
}

// LooseEnemiesTank has no ready Taunt: Revenge when it is ready, else Sunder
// Armor (the tests above).
TEST(WarriorTankLooseEnemy, UsesRevengeOnItsLooseEnemyWhenTauntIsNotReady)
{
    LooseEnemiesTank tank;
    tank.fake.SetCannotCast(Warrior::Revenge, false);
    tank.fake.SetVictim(Bat, Mage);

    EXPECT_EQ(tank.ThinkAt(0), (std::vector<std::string>{CastOn(Warrior::Revenge, Bat)}));
}

// Mocking Blow is known and castable, Taunt waits: Sunder Armor, never
// Mocking Blow.
TEST(WarriorTankLooseEnemy, NeverUsesMockingBlow)
{
    LooseEnemiesTank tank;
    tank.fake.SetKnownSpell(Warrior::MockingBlow);
    tank.fake.SetVictim(Bat, Healer);

    EXPECT_EQ(tank.ThinkAt(0), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Bat)}));
    for (std::int64_t ms = 500; ms < 5000; ms += 500)
        for (std::string const& deed : tank.ThinkAt(ms))
            EXPECT_EQ(deed.find("cast 694 "), std::string::npos) << deed;
}

// The stop case: no ability can go out on the loose enemy (Taunt, Revenge
// and Sunder Armor all wait): the tank casts nothing on it.
TEST(WarriorTankLooseEnemy, CastsNothingOnItsLooseEnemyWhenNoAbilityCanGoOut)
{
    LooseEnemiesTank tank;
    tank.fake.SetCannotCast(Warrior::SunderArmor);
    tank.fake.SetVictim(Bat, Healer);

    for (std::string const& deed : tank.ThinkAt(0))
        EXPECT_EQ(deed.find("cast "), std::string::npos) << deed;
}

TEST(WarriorTankLooseEnemy, GoesBackToTheFocusWhenNoLooseEnemyIsLeft)
{
    LooseEnemiesTank tank;
    tank.fake.SetVictim(Bat, Mage);
    ASSERT_EQ(tank.ThinkAt(0), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Bat)}));
    tank.fake.SetHealth(Bat, 0, 300);
    tank.fake.SetVictim(Bat, NoGuid);

    EXPECT_EQ(tank.ThinkAt(500), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));
}

// A picked-up enemy turns to the tank before the tank hit it with an
// ability (join_three run 1): the tank casts one on it all the same, then
// goes back to the focus. No turn to a threat target without a cast.
TEST(WarriorTankLooseEnemy, CastsOnAPickedUpEnemyThatComesToItBeforeItGoesBackToTheFocus)
{
    // jvbastel/WOWAI#169: the healer is stunned, so the tank goes to it.
    BatOnTheFarHealer tank;
    tank.fake.SetRootedOrStunned(Healer);
    tank.fake.SetPower(Tank, 30, 100);
    tank.fake.SetCannotCast(Warrior::Revenge);
    tank.Think();
    ASSERT_EQ(tank.fake.Deeds(), (std::vector<std::string>{Attack(Bat), Chase(Bat)}));
    tank.fake.SetVictim(Tank, Bat);
    tank.fake.SetDistance(Bat, 2.0f);
    tank.fake.SetVictim(Bat, Tank);
    tank.fake.SetThreatRatio(Tank, Bat, 2.0f);

    tank.fake.ClearDeeds();
    tank.nowMs = 300;
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Bat)}));

    tank.fake.ClearDeeds();
    tank.nowMs = 600;
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{Attack(Kobold), Chase(Kobold)}));
}

// join_four_healer: the picked-up enemy comes to the tank, and the pick goes
// on to the next loose enemy. The enemy that the tank turned to gets its
// ability first.
TEST(WarriorTankLooseEnemy, CastsOnThePickedUpEnemyBeforeItTakesTheNextLooseEnemy)
{
    // jvbastel/WOWAI#169: the healer is stunned, so the tank goes to it.
    BatOnTheFarHealer tank;
    tank.fake.SetRootedOrStunned(Healer);
    tank.fake.SetPower(Tank, 30, 100);
    tank.fake.SetCannotCast(Warrior::Revenge);
    tank.Think();
    ASSERT_EQ(tank.fake.Deeds(), (std::vector<std::string>{Attack(Bat), Chase(Bat)}));
    tank.fake.SetVictim(Tank, Bat);
    tank.fake.SetDistance(Bat, 2.0f);
    tank.fake.SetVictim(Bat, Tank);
    tank.fake.SetThreatRatio(Tank, Bat, 2.0f);
    tank.fake.SetCannotCast(Warrior::Taunt);
    tank.fake.SetVictim(Wolf, Healer);
    tank.fake.SetDistance(Wolf, 2.0f);

    tank.fake.ClearDeeds();
    tank.nowMs = 300;
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Bat)}));

    tank.fake.ClearDeeds();
    tank.nowMs = 600;
    tank.Think();
    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Wolf)}));
}

// jvbastel/WOWAI#169: the tank holds its fight spot. It acts on a loose
// enemy only once that enemy is in range, and goes out only to a member
// that cannot move (rooted or stunned).
namespace
{
    // The deeds of `deeds` hold no attack of, and no chase of, `enemy`.
    void ExpectNoMoveTo(std::vector<std::string> const& deeds, Bots::Guid enemy)
    {
        for (std::string const& deed : deeds)
        {
            EXPECT_NE(deed, Attack(enemy));
            EXPECT_NE(deed, Chase(enemy));
        }
    }

    bool Has(std::vector<std::string> const& deeds, std::string const& deed)
    {
        return std::find(deeds.begin(), deeds.end(), deed) != deeds.end();
    }
}

// Taunt waits for its cooldown, and the bat on the healer is out of melee
// reach: the tank stays on the focus.
TEST(WarriorTankHoldsItsSpot, DoesNotGoToALooseEnemyOutOfRange)
{
    LooseEnemiesTank tank;
    tank.fake.SetVictim(Bat, Healer);
    tank.fake.SetDistance(Bat, 15.0f);

    std::vector<std::string> const deeds = tank.ThinkAt(0);

    ExpectNoMoveTo(deeds, Bat);
    EXPECT_EQ(deeds, (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));
}

// Taunt is ready, but the bat is out of its 20 yards.
TEST(WarriorTankHoldsItsSpot, DoesNotGoToALooseEnemyOutOfTauntRange)
{
    BatOnTheFarHealer tank;

    tank.Think();

    ExpectNoMoveTo(tank.fake.Deeds(), Bat);
}

// The healer brings the bat to the tank: once in reach, it gets the pickup
// ability.
TEST(WarriorTankHoldsItsSpot, ActsOnTheLooseEnemyOnceItIsInRange)
{
    LooseEnemiesTank tank;
    tank.fake.SetVictim(Bat, Healer);
    tank.fake.SetDistance(Bat, 15.0f);
    ASSERT_EQ(tank.ThinkAt(0), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Kobold)}));
    tank.fake.SetDistance(Bat, 2.0f);

    EXPECT_EQ(tank.ThinkAt(500), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Bat)}));
}

TEST(WarriorTankHoldsItsSpot, TauntsTheLooseEnemyOnceItIsInTauntRange)
{
    BatOnTheFarHealer tank;
    tank.Think();
    tank.fake.SetDistance(Bat, 15.0f);
    tank.fake.ClearDeeds();
    tank.nowMs = 500;

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{CastOn(Warrior::Taunt, Bat)}));
}

// An enemy on the healer out of range does not hold the tank: it takes the
// enemy in reach meanwhile.
TEST(WarriorTankHoldsItsSpot, TakesALooseEnemyInRangeBeforeOneOfAHigherRankOutOfRange)
{
    LooseEnemiesTank tank;
    tank.fake.SetVictim(Bat, Healer);
    tank.fake.SetDistance(Bat, 15.0f);
    tank.fake.SetVictim(Wolf, Rogue);

    EXPECT_EQ(tank.ThinkAt(0), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Wolf)}));
}

TEST(WarriorTankHoldsItsSpot, GoesToTheAttackerOfARootedMember)
{
    LooseEnemiesTank tank;
    tank.fake.SetVictim(Bat, Mage);
    tank.fake.SetDistance(Bat, 15.0f);
    tank.fake.SetRootedOrStunned(Mage);

    std::vector<std::string> const deeds = tank.ThinkAt(0);

    EXPECT_TRUE(Has(deeds, Attack(Bat)));
    EXPECT_TRUE(Has(deeds, Chase(Bat)));
}

// Taunt is ready, the healer is stunned 28 yards away: the tank goes to it.
TEST(WarriorTankHoldsItsSpot, GoesToTheAttackerOfAStunnedHealerOutOfTauntRange)
{
    BatOnTheFarHealer tank;
    tank.fake.SetRootedOrStunned(Healer);

    tank.Think();

    EXPECT_EQ(tank.fake.Deeds(), (std::vector<std::string>{Attack(Bat), Chase(Bat)}));
}

// The walk back after a fallback pull does not wait for a loose enemy out of
// range: the healer brings it to the fight spot.
TEST(WarriorTankHoldsItsSpot, WalksBackToTheFightSpotWhileALooseEnemyIsOutOfRange)
{
    BowTank tank;
    tank.fake.SetRangedWeapon(Bots::RangedWeapon{});
    tank.fake.SetGroupMembers({Healer});
    tank.group.SetHealer(Healer);
    tank.fake.SetPosition(Tank, {10, 20, 0});
    tank.Pull();
    tank.fake.SetPosition(Tank, {28, 20, 0});
    tank.fake.SetDistance(Kobold, 2.0f);
    tank.fake.SetInCombat(Tank);
    tank.fake.SetVictim(Tank, Kobold);
    tank.fake.SetVictim(Kobold, Tank);
    tank.fake.SetAura(Tank, Warrior::BattleStance, false);
    tank.fake.SetAura(Tank, Warrior::DefensiveStance);
    tank.fake.SetAura(Tank, Warrior::BattleShout);
    tank.fake.SetPower(Tank, 20, 100);
    tank.fake.SetHealth(Healer, 300, 300);
    tank.fake.SetHealth(Wolf, 300, 300);
    tank.fake.SetSpellRange(Warrior::Taunt, 20.0f);
    tank.fake.SetSpellRange(Warrior::SunderArmor, 5.0f);
    tank.fake.SetSpellRange(Warrior::Revenge, 5.0f);
    tank.fake.SetDistance(Wolf, 28.0f);
    tank.fake.SetVictim(Wolf, Healer);
    ASSERT_EQ(tank.Think(), "warn when a pack joins");
    ASSERT_EQ(tank.Think(), "hold the pull");
    tank.fake.ClearDeeds();

    EXPECT_EQ(tank.Think(), "walk back to the fight spot");
    ExpectNoMoveTo(tank.fake.Deeds(), Wolf);
}

// A lack of rage does not drop the tank's pick in melee reach, so it does not
// restart the pick's 4 s: after them the tank takes the wolf (jvbastel/WOWAI#169
// review).
TEST(WarriorTankHoldsItsSpot, ALackOfRageDoesNotRestartThePickInMeleeReach)
{
    LooseEnemiesTank tank;
    tank.fake.SetVictim(Bat, Healer);
    tank.fake.SetVictim(Wolf, Rogue);
    ASSERT_EQ(tank.ThinkAt(0), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Bat)}));
    tank.fake.SetCannotCast(Warrior::SunderArmor);
    ASSERT_EQ(tank.ThinkAt(2000), std::vector<std::string>{});
    tank.fake.SetCannotCast(Warrior::SunderArmor, false);

    EXPECT_EQ(tank.ThinkAt(4500), (std::vector<std::string>{CastOn(Warrior::SunderArmor, Wolf)}));
}
