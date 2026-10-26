#include "ClassModules.h"
#include "DeathAndRest.h"
#include "FakeBot.h"
#include "PriestModule.h"

#include <gtest/gtest.h>

using Bots::BotContext;
using Bots::CommandState;
using Bots::GroupCoordinator;
using Bots::GroupEvent;
using Bots::GroupEventKind;
using Bots::LifeState;
using Bots::NoGuid;
using Bots::RestReason;
using Bots::StrategyStack;
using Bots::Tests::FakeBot;
namespace Priest = Bots::Priest;

namespace
{
    Bots::Guid const Player = 0x05;
    Bots::Guid const Tank = 0x11;
    Bots::Guid const Healer = 0x12;
    Bots::Guid const Mage = 0x13;

    // A bot of a group of four: the player (the leader), a tank, a Priest
    // healer, and a mage. Everyone is alive and at full health and mana,
    // nobody fights, and the bot follows the player.
    struct GroupBot
    {
        FakeBot fake;
        GroupCoordinator group;
        StrategyStack stack;
        Bots::Guid self;

        GroupBot(Bots::Guid self, Bots::BotClass botClass, Bots::BotRole role) :
            fake(self), stack(Bots::MakeStrategyStack(botClass, role)), self(self)
        {
            std::vector<Bots::Guid> others;
            for (Bots::Guid member : {Player, Tank, Healer, Mage})
            {
                fake.SetHealth(member, 600, 600);
                fake.SetDistance(member, 10.0f);
                fake.SetAura(member, Priest::PowerWordFortitude);
                if (member != self)
                    others.push_back(member);
            }
            fake.SetGroupMembers(others);
            fake.SetGroupLeader(Player);
            fake.SetCanResurrect(Healer);
            fake.SetMana(Healer, 400, 400);
            fake.SetMana(Mage, 400, 400);
            fake.SetPower(Self(), 400, 400);
            fake.SetFollowTarget(Player);
        }

        Bots::Guid Self() const { return self; }

        std::string Think()
        {
            BotContext const context{fake, fake, group, CommandState::Follow, Player};
            return stack.Think(context);
        }

        void Watch() { Bots::WatchGroup(fake, group); }

        void Die(Bots::Guid member, LifeState state = LifeState::Dead)
        {
            fake.SetHealth(member, 0, 600);
            fake.SetLifeState(member, state);
        }
    };

    struct DeadMage : GroupBot
    {
        DeadMage() : GroupBot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged) { Die(Mage); }
    };

    bool IsRest(GroupEvent const& event, Bots::Guid bot, RestReason reason)
    {
        return event.kind == GroupEventKind::Rest && event.bot == bot && event.reason == reason;
    }
}

// Death and release

TEST(Death, ADeadBotReleasesWhenTheLeaderBecomesAGhost)
{
    DeadMage bot;
    bot.Die(Player, LifeState::Ghost);

    EXPECT_EQ(bot.Think(), "release with the leader");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"release"}));
}

TEST(Death, ADeadBotWaitsWhileThePriestCanResurrectIt)
{
    DeadMage bot;
    bot.Die(Player);

    EXPECT_EQ(bot.Think(), "wait dead");
    EXPECT_TRUE(bot.fake.Deeds().empty());
}

TEST(Death, ADeadBotDoesNothingElse)
{
    GroupBot bot(Healer, Bots::BotClass::Priest, Bots::BotRole::Healer);
    bot.fake.SetKnownSpell(Priest::Heal);
    bot.fake.SetHealth(Tank, 100, 600);
    bot.fake.SetInCombat(Tank);
    bot.fake.SetFollowTarget(NoGuid);
    bot.Die(Healer);

    EXPECT_EQ(bot.Think(), "wait dead");
    EXPECT_TRUE(bot.fake.Deeds().empty());
}

// No bot revives at its corpse on its own. A ghost runs back from the
// graveyard to the dungeon, as a player does.
TEST(Death, AGhostWalksBackToTheDungeon)
{
    DeadMage bot;
    bot.fake.SetLifeState(Mage, LifeState::Ghost);

    EXPECT_EQ(bot.Think(), "walk back as a ghost");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"walk as a ghost"}));
}

TEST(Death, AGhostNearTheLivingLeaderDoesNotReviveAtItsCorpse)
{
    DeadMage bot;
    bot.fake.SetLifeState(Mage, LifeState::Ghost);
    bot.fake.SetDistance(Player, 5.0f);

    EXPECT_EQ(bot.Think(), "walk back as a ghost");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"walk as a ghost"}));
}

TEST(Death, WithNobodyToResurrectADeadBotReleasesAfterTheFight)
{
    DeadMage bot;
    bot.Die(Healer);

    EXPECT_EQ(bot.Think(), "release when no resurrect comes");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"release"}));
    std::vector<GroupEvent> const events = bot.group.TakeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(IsRest(events[0], Mage, RestReason::Revive));
}

TEST(Death, WithoutAPriestADeadBotReleasesAfterTheFight)
{
    DeadMage bot;
    bot.fake.SetCanResurrect(Healer, false);

    EXPECT_EQ(bot.Think(), "release when no resurrect comes");
}

TEST(Death, WithNobodyToResurrectADeadBotWaitsForTheEndOfTheFight)
{
    DeadMage bot;
    bot.Die(Healer);
    bot.fake.SetInCombat(Tank);

    EXPECT_EQ(bot.Think(), "wait dead");
}

// The Priest finds no safe place to cast from: the resurrect never comes,
// so the dead bot does not keep the others waiting.
TEST(Death, ADeadBotReleasesWhenThePriestGivesUpItsResurrect)
{
    DeadMage bot;
    bot.group.GiveUpResurrect(Mage);

    EXPECT_EQ(bot.Think(), "release when no resurrect comes");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"release"}));
}

TEST(Death, ABotThatLivesAgainCanBeResurrectedAgainLater)
{
    DeadMage bot;
    bot.group.GiveUpResurrect(Mage);
    bot.fake.SetHealth(Mage, 300, 600);
    bot.fake.SetLifeState(Mage, LifeState::Alive);
    bot.Watch();

    bot.Die(Mage);
    EXPECT_EQ(bot.Think(), "wait dead");
}

TEST(Death, InAWipeTheBotsWaitForTheLeaderToRelease)
{
    DeadMage bot;
    for (Bots::Guid member : {Player, Tank, Healer})
        bot.Die(member);

    EXPECT_EQ(bot.Think(), "wait dead");
}

TEST(Death, ALivingBotIgnoresTheDeathRules)
{
    GroupBot bot(Tank, Bots::BotClass::Warrior, Bots::BotRole::Tank);
    bot.Die(Player, LifeState::Ghost);

    EXPECT_NE(bot.Think(), "release with the leader");
    EXPECT_TRUE(bot.fake.Deeds().empty());
}

// The Priest resurrects after a fight.

TEST(Resurrect, ThePriestResurrectsADeadMemberAfterTheFight)
{
    GroupBot bot(Healer, Bots::BotClass::Priest, Bots::BotRole::Healer);
    bot.fake.SetKnownSpell(Priest::Resurrection);
    bot.fake.SetCastsOnDead(Priest::Resurrection);
    bot.fake.SetAura(Healer, Priest::InnerFire);
    bot.Die(Mage);

    EXPECT_EQ(bot.Think(), "resurrect a member");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"cast 2006 on 0x0000000000000013"}));
}

TEST(Resurrect, ThePriestDoesNotResurrectInAFight)
{
    GroupBot bot(Healer, Bots::BotClass::Priest, Bots::BotRole::Healer);
    bot.fake.SetKnownSpell(Priest::Resurrection);
    bot.fake.SetCastsOnDead(Priest::Resurrection);
    bot.fake.SetAura(Healer, Priest::InnerFire);
    bot.Die(Mage);
    bot.fake.SetInCombat(Tank);

    EXPECT_NE(bot.Think(), "resurrect a member");
}

TEST(Resurrect, ThePriestDoesNotResurrectAGhost)
{
    GroupBot bot(Healer, Bots::BotClass::Priest, Bots::BotRole::Healer);
    bot.fake.SetKnownSpell(Priest::Resurrection);
    bot.fake.SetCastsOnDead(Priest::Resurrection);
    bot.fake.SetAura(Healer, Priest::InnerFire);
    bot.Die(Mage, LifeState::Ghost);

    EXPECT_NE(bot.Think(), "resurrect a member");
}

namespace
{
    // The Priest of the group, out of combat with its buffs, which knows
    // Resurrection.
    struct GroupPriest : GroupBot
    {
        GroupPriest() : GroupBot(Healer, Bots::BotClass::Priest, Bots::BotRole::Healer)
        {
            fake.SetKnownSpell(Priest::Resurrection);
            fake.SetCastsOnDead(Priest::Resurrection);
            fake.SetAura(Healer, Priest::InnerFire);
        }

        // Member `member` died out of the reach of the resurrect, 50
        // yards east of the Priest at the origin.
        void DieOutOfReach(Bots::Guid member)
        {
            Die(member);
            fake.SetDistance(member, 50.0f);
            fake.SetPosition(member, {50.0f, 0.0f, 0.0f});
            fake.SetCannotCastOn(Priest::Resurrection, member);
        }
    };
}

// Today's DeadMember picked the first dead member only: a member in reach
// behind one out of reach was never resurrected.
TEST(Resurrect, ThePriestResurrectsADeadMemberInReachBehindOneOutOfReach)
{
    GroupPriest bot;
    bot.DieOutOfReach(Tank);
    bot.Die(Mage);

    EXPECT_EQ(bot.Think(), "resurrect a member");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"cast 2006 on 0x0000000000000013"}));
}

TEST(Resurrect, AfterTheFightThePriestWalksToASafePlaceNearACorpseOutOfReach)
{
    GroupPriest bot;
    bot.DieOutOfReach(Mage);

    EXPECT_EQ(bot.Think(), "go to a corpse out of reach");
    // The safe place nearest to the Priest (SafePlace.h), 0.9 of the 26
    // yards of the cast from the corpse.
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"keep move to 26.6 0 0"}));
    EXPECT_FALSE(bot.group.IsResurrectGivenUp(Mage));
}

// A corpse in reach but out of sight: CanCast fails, so the resurrect rule
// skips it. The Priest walks to a place that sees it, or gives it up, so the
// dead member never waits for a resurrect that cannot come.
TEST(Resurrect, ThePriestWalksToACorpseInReachButOutOfSight)
{
    GroupPriest bot;
    bot.Die(Mage);
    bot.fake.SetCannotCastOn(Priest::Resurrection, Mage);
    // The Priest 60 yards east of the corpse, where the corpse is out of
    // sight from x = 50 on.
    bot.fake.SetPosition(Healer, {60.0f, 0.0f, 0.0f});
    bot.fake.SetPosition(Mage, {0.0f, 0.0f, 0.0f});
    bot.fake.AddBlindFromX(50.0f);

    EXPECT_EQ(bot.Think(), "go to a corpse out of reach");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"keep move to 23.4 0 0"}));
}

TEST(Resurrect, ThePriestDoesNotWalkToACorpseInAFight)
{
    GroupPriest bot;
    bot.DieOutOfReach(Mage);
    bot.fake.SetInCombat(Tank);

    EXPECT_NE(bot.Think(), "go to a corpse out of reach");
}

// A corpse next to a living pack, or in lava: the Priest does not go, and
// the dead member runs back as a ghost instead.
TEST(Resurrect, WithNoSafePlaceInCastRangeThePriestGivesUpTheResurrect)
{
    GroupPriest bot;
    bot.DieOutOfReach(Mage);
    bot.fake.SetPosition(Healer, {1.0f, 2.0f, 3.0f});
    bot.fake.AddLavaFromX(-1000.0f);

    bot.Think();

    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"move to 1 2 3"}));
    EXPECT_TRUE(bot.group.IsResurrectGivenUp(Mage));
    EXPECT_NE(bot.Think(), "go to a corpse out of reach");
}

// An enemy came near the place on the way: the Priest stops where it is.
TEST(Resurrect, ThePriestStopsAndGivesUpWhenThePlaceIsNoLongerSafe)
{
    GroupPriest bot;
    bot.DieOutOfReach(Mage);
    bot.fake.SetPosition(Healer, {1.0f, 2.0f, 3.0f});
    bot.Think();
    bot.fake.ClearDeeds();

    // A pack with a long aggro distance came to the corpse.
    bot.fake.AddEnemy({50.0f, 0.0f, 0.0f}, 30.0f);
    bot.Think();

    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"move to 1 2 3"}));
    EXPECT_TRUE(bot.group.IsResurrectGivenUp(Mage));
}

TEST(Resurrect, ThePriestDoesNotResurrectAMemberThatItGaveUp)
{
    GroupPriest bot;
    bot.Die(Mage);
    bot.group.GiveUpResurrect(Mage);

    EXPECT_NE(bot.Think(), "resurrect a member");
}

// Rest

TEST(Rest, ABotBelowHalfHealthEatsAfterAFight)
{
    GroupBot bot(Tank, Bots::BotClass::Warrior, Bots::BotRole::Tank);
    bot.fake.SetHealth(Tank, 290, 600);

    EXPECT_EQ(bot.Think(), "eat");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"eat"}));
}

TEST(Rest, ABotBelowHalfManaDrinksAfterAFight)
{
    GroupBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    bot.fake.SetMana(Mage, 150, 400);

    EXPECT_EQ(bot.Think(), "drink");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"drink"}));
}

TEST(Rest, ABotAtHalfHealthAndManaDoesNotRest)
{
    GroupBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    bot.fake.SetHealth(Mage, 300, 600);
    bot.fake.SetMana(Mage, 200, 400);

    EXPECT_EQ(bot.Think(), "");
}

TEST(Rest, ABotWithoutManaDoesNotDrink)
{
    GroupBot bot(Tank, Bots::BotClass::Warrior, Bots::BotRole::Tank);
    bot.fake.SetPower(Tank, 0, 100);

    EXPECT_EQ(bot.Think(), "");
}

TEST(Rest, ABotDoesNotRestWhileTheGroupFights)
{
    GroupBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    bot.fake.SetHealth(Mage, 100, 600);
    bot.fake.SetInCombat(Tank);

    EXPECT_NE(bot.Think(), "eat");
}

TEST(Rest, AnEatingBotKeepsEatingInsteadOfFollowing)
{
    GroupBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    bot.fake.SetHealth(Mage, 400, 600);
    bot.fake.SetEating();
    bot.fake.SetFollowTarget(NoGuid);

    EXPECT_EQ(bot.Think(), "keep resting");
    EXPECT_TRUE(bot.fake.Deeds().empty());
}

TEST(Rest, ABotThatIsFullStopsResting)
{
    GroupBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    bot.fake.SetEating();
    bot.fake.SetFollowTarget(NoGuid);

    EXPECT_EQ(bot.Think(), "follow the player");
}

TEST(Rest, ADrinkingBotEatsTooWhenItsHealthIsLow)
{
    GroupBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    bot.fake.SetMana(Mage, 300, 400);
    bot.fake.SetDrinking();
    bot.fake.SetHealth(Mage, 200, 600);

    EXPECT_EQ(bot.Think(), "eat");
}

// The group watch: the wipe and rest moments of the coordinator.

TEST(GroupWatch, TheCoordinatorSeesAWipeOnce)
{
    DeadMage bot;
    for (Bots::Guid member : {Player, Tank, Healer})
        bot.Die(member);

    bot.Watch();
    bot.Watch();

    EXPECT_TRUE(bot.group.IsWiped());
    std::vector<GroupEvent> const events = bot.group.TakeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, GroupEventKind::Wipe);
    EXPECT_TRUE(bot.group.TakeEvents().empty());
}

TEST(GroupWatch, AGhostCountsAsDeadInAWipe)
{
    DeadMage bot;
    bot.Die(Player, LifeState::Ghost);
    bot.Die(Tank);
    bot.Die(Healer);

    bot.Watch();

    EXPECT_TRUE(bot.group.IsWiped());
}

TEST(GroupWatch, AWipeEndsWhenAMemberLivesAgain)
{
    DeadMage bot;
    for (Bots::Guid member : {Player, Tank, Healer})
        bot.Die(member);
    bot.Watch();
    bot.group.TakeEvents();

    bot.fake.SetHealth(Player, 300, 600);
    bot.fake.SetLifeState(Player, LifeState::Alive);
    bot.Watch();
    EXPECT_FALSE(bot.group.IsWiped());

    bot.Die(Player);
    bot.Watch();
    EXPECT_EQ(bot.group.TakeEvents().size(), 1u);
}

TEST(GroupWatch, NoWipeWhileOneMemberLives)
{
    DeadMage bot;
    bot.Die(Player);
    bot.Die(Tank);

    bot.Watch();

    EXPECT_FALSE(bot.group.IsWiped());
    EXPECT_TRUE(bot.group.TakeEvents().empty());
}

TEST(GroupWatch, TheCoordinatorSeesABotStartToEatOnce)
{
    GroupBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    bot.fake.SetHealth(Mage, 200, 600);
    bot.fake.SetMana(Mage, 100, 400);
    bot.fake.SetEating();

    bot.Watch();
    bot.fake.SetDrinking();
    bot.Watch();

    EXPECT_EQ(bot.group.GetResting(), (std::vector<Bots::Guid>{Mage}));
    std::vector<GroupEvent> const events = bot.group.TakeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(IsRest(events[0], Mage, RestReason::Eat));
}

TEST(GroupWatch, TheCoordinatorSeesABotStartToDrink)
{
    GroupBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    bot.fake.SetMana(Mage, 100, 400);
    bot.fake.SetDrinking();

    bot.Watch();

    std::vector<GroupEvent> const events = bot.group.TakeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(IsRest(events[0], Mage, RestReason::Drink));
}

TEST(GroupWatch, ABotThatStopsEatingNoLongerRests)
{
    GroupBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    bot.fake.SetHealth(Mage, 200, 600);
    bot.fake.SetEating();
    bot.Watch();

    bot.fake.SetEating(false);
    bot.Watch();

    EXPECT_TRUE(bot.group.GetResting().empty());
}

// A bot that is full again stops resting, even while its food or drink aura
// lasts: a GM `.replenish` or a heal fills it before the meal ends. Else the
// group "rests" and the tank refuses `pull` with "We are resting."

TEST(GroupWatch, AFullBotWithAFoodAuraDoesNotRest)
{
    GroupBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    bot.fake.SetEating();
    bot.fake.SetDrinking();

    bot.Watch();

    EXPECT_TRUE(bot.group.GetResting().empty());
    EXPECT_TRUE(bot.group.TakeEvents().empty());
}

TEST(GroupWatch, AnEatingBotWithFullHealthRestsWhileItDrinksForMana)
{
    GroupBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    bot.fake.SetMana(Mage, 300, 400);
    bot.fake.SetEating();
    bot.fake.SetDrinking();

    bot.Watch();

    EXPECT_EQ(bot.group.GetResting(), (std::vector<Bots::Guid>{Mage}));
    std::vector<GroupEvent> const events = bot.group.TakeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(IsRest(events[0], Mage, RestReason::Drink));
}

TEST(GroupWatch, ABotThatEatsUntilFullStopsResting)
{
    GroupBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    bot.fake.SetHealth(Mage, 200, 600);
    bot.fake.SetEating();
    bot.Watch();

    bot.fake.SetHealth(Mage, 600, 600);
    bot.Watch();

    EXPECT_TRUE(bot.group.GetResting().empty());
}

TEST(Rest, RestsNowOnlyWhileTheMealStillFillsTheBot)
{
    FakeBot fake(Mage);
    fake.SetHealth(Mage, 600, 600);
    fake.SetMana(Mage, 400, 400);
    EXPECT_FALSE(Bots::RestsNow(fake));

    fake.SetEating();
    EXPECT_FALSE(Bots::RestsNow(fake));
    fake.SetHealth(Mage, 599, 600);
    EXPECT_TRUE(Bots::RestsNow(fake));

    fake.SetEating(false);
    fake.SetDrinking();
    EXPECT_FALSE(Bots::RestsNow(fake));
    fake.SetMana(Mage, 399, 400);
    EXPECT_TRUE(Bots::RestsNow(fake));
}

TEST(Rest, ADeadBotDoesNotRest)
{
    FakeBot fake(Mage);
    fake.SetHealth(Mage, 0, 600);
    fake.SetLifeState(Mage, LifeState::Dead);
    fake.SetEating();

    EXPECT_FALSE(Bots::RestsNow(fake));
}

// A member that the run revives (the scripted rule) comes back at half
// health and mana. It rests until full before the next pull.

TEST(Rest, ABotRevivedByTheRunEatsAtHalfHealth)
{
    GroupBot bot(Tank, Bots::BotClass::Warrior, Bots::BotRole::Tank);
    bot.fake.SetHealth(Tank, 300, 600);
    bot.group.SetRestUntilFull(Tank);

    EXPECT_EQ(bot.Think(), "eat");
}

TEST(Rest, ABotRevivedByTheRunDrinksAtHalfMana)
{
    GroupBot bot(Mage, Bots::BotClass::Mage, Bots::BotRole::Ranged);
    bot.fake.SetMana(Mage, 200, 400);
    bot.group.SetRestUntilFull(Mage);

    EXPECT_EQ(bot.Think(), "drink");
}

TEST(GroupWatch, ABotRevivedByTheRunRestsBeforeItsFirstMeal)
{
    // The run waits for it from the revive on, not from its next think.
    GroupBot bot(Tank, Bots::BotClass::Warrior, Bots::BotRole::Tank);
    bot.fake.SetHealth(Tank, 300, 600);
    bot.group.SetRestUntilFull(Tank);

    EXPECT_EQ(bot.group.GetResting(), (std::vector<Bots::Guid>{Tank}));
    bot.Watch();
    EXPECT_EQ(bot.group.GetResting(), (std::vector<Bots::Guid>{Tank}));
}

TEST(GroupWatch, ABotRevivedByTheRunRestsOnlyUntilFull)
{
    GroupBot bot(Tank, Bots::BotClass::Warrior, Bots::BotRole::Tank);
    bot.fake.SetHealth(Tank, 300, 600);
    bot.group.SetRestUntilFull(Tank);
    bot.fake.SetEating();
    bot.Watch();

    bot.fake.SetHealth(Tank, 600, 600);
    bot.Watch();
    EXPECT_TRUE(bot.group.GetResting().empty());

    // The next time at half health it is a normal bot again.
    bot.fake.SetEating(false);
    bot.fake.SetHealth(Tank, 300, 600);
    EXPECT_EQ(bot.Think(), "");
}
