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

TEST(Death, AGhostRevivesAtItsCorpseWhenTheLeaderIsAliveNearIt)
{
    DeadMage bot;
    bot.fake.SetLifeState(Mage, LifeState::Ghost);
    bot.fake.SetCorpseDistance(Player, 25.0f);

    EXPECT_EQ(bot.Think(), "revive at the corpse");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"revive at the corpse"}));
    std::vector<GroupEvent> const events = bot.group.TakeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(IsRest(events[0], Mage, RestReason::Revive));
}

TEST(Death, AGhostWaitsWhileTheLeaderIsFarFromItsCorpse)
{
    DeadMage bot;
    bot.fake.SetLifeState(Mage, LifeState::Ghost);
    bot.fake.SetCorpseDistance(Player, 45.0f);

    EXPECT_EQ(bot.Think(), "wait dead");
}

TEST(Death, AGhostWaitsWhileTheLeaderIsAGhostNearItsCorpse)
{
    DeadMage bot;
    bot.fake.SetLifeState(Mage, LifeState::Ghost);
    bot.Die(Player, LifeState::Ghost);
    bot.fake.SetCorpseDistance(Player, 5.0f);

    EXPECT_EQ(bot.Think(), "wait dead");
}

TEST(Death, WithNobodyToResurrectADeadBotRevivesAtItsCorpseAtOnceAfterTheFight)
{
    DeadMage bot;
    bot.Die(Healer);

    EXPECT_EQ(bot.Think(), "revive when nobody can resurrect");
    EXPECT_EQ(bot.fake.Deeds(), (std::vector<std::string>{"revive at the corpse"}));
    std::vector<GroupEvent> const events = bot.group.TakeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(IsRest(events[0], Mage, RestReason::Revive));
}

TEST(Death, WithoutAPriestADeadBotRevivesAtOnce)
{
    DeadMage bot;
    bot.fake.SetCanResurrect(Healer, false);

    EXPECT_EQ(bot.Think(), "revive when nobody can resurrect");
}

TEST(Death, WithNobodyToResurrectADeadBotWaitsForTheEndOfTheFight)
{
    DeadMage bot;
    bot.Die(Healer);
    bot.fake.SetInCombat(Tank);

    EXPECT_EQ(bot.Think(), "wait dead");
}

// The last bot that fought died: only the player lives, out of combat, but
// the enemy still fights or runs home next to the corpse. That is no end of
// the fight, so the dead bots wait. (A tank revived next to the boss in #40.)
TEST(Death, ADeadBotWaitsWhileAnEnemyFightsNearIt)
{
    DeadMage bot;
    bot.Die(Healer);
    bot.Die(Tank);
    bot.fake.SetFightNear(20.0f);

    EXPECT_EQ(bot.Think(), "wait dead");
    EXPECT_TRUE(bot.fake.Deeds().empty());
}

TEST(Death, ADeadBotRevivesWhenTheNearestFightIsFarAway)
{
    DeadMage bot;
    bot.Die(Healer);
    bot.fake.SetFightNear(Bots::ReviveClearOfFightYards + 5.0f);

    EXPECT_EQ(bot.Think(), "revive when nobody can resurrect");
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
