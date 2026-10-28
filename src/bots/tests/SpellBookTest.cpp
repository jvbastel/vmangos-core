#include "SpellBook.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

using Bots::HighestKnownRank;
using Bots::NoSpell;
using Bots::Tests::FakeBot;

namespace
{
    Bots::Guid const Self = 0x11;
    // Heroic Strike, ranks 1 to 3.
    Bots::SpellId const HeroicStrike1 = 78;
    Bots::SpellId const HeroicStrike2 = 284;
    Bots::SpellId const HeroicStrike3 = 285;

    FakeBot WarriorWithChain()
    {
        FakeBot bot(Self);
        bot.SetSpellChain({HeroicStrike1, HeroicStrike2, HeroicStrike3});
        return bot;
    }
}

TEST(SpellBook, FindsTheHighestKnownRankFromTheFirstRank)
{
    FakeBot bot = WarriorWithChain();
    bot.SetKnownSpell(HeroicStrike1);
    bot.SetKnownSpell(HeroicStrike2);

    EXPECT_EQ(HighestKnownRank(bot, HeroicStrike1), HeroicStrike2);
}

TEST(SpellBook, AnUnknownSpellHasNoRank)
{
    FakeBot bot = WarriorWithChain();

    EXPECT_EQ(HighestKnownRank(bot, HeroicStrike1), NoSpell);
}

TEST(SpellBook, ASpellWithoutRanksIsItsOwnHighestRank)
{
    FakeBot bot(Self);
    Bots::SpellId const Taunt = 355;
    bot.SetKnownSpell(Taunt);

    EXPECT_EQ(HighestKnownRank(bot, Taunt), Taunt);
}

TEST(SpellBook, SkipsAMissingRankInTheMiddle)
{
    FakeBot bot = WarriorWithChain();
    bot.SetKnownSpell(HeroicStrike1);
    bot.SetKnownSpell(HeroicStrike3);

    EXPECT_EQ(HighestKnownRank(bot, HeroicStrike1), HeroicStrike3);
}

// jvbastel/WOWAI#134: the rank choice of a heal by the missing health of the
// target (CastableHealRank).
namespace
{
    Bots::Guid const Tank = 0x22;
    // Lesser Heal, ranks 1 to 3, with their average heal and mana cost in
    // 1.12 (notes: research-holy-priest-healing-1.12.md).
    Bots::SpellId const LesserHeal1 = 2050;
    Bots::SpellId const LesserHeal2 = 2052;
    Bots::SpellId const LesserHeal3 = 2053;

    // A priest that knows the three ranks, with full mana, and a tank that
    // misses `missing` of 600 health.
    FakeBot PriestWithTankMissing(std::uint32_t missing)
    {
        FakeBot bot(Self);
        bot.SetSpellChain({LesserHeal1, LesserHeal2, LesserHeal3});
        for (Bots::SpellId rank : {LesserHeal1, LesserHeal2, LesserHeal3})
            bot.SetKnownSpell(rank);
        bot.SetRankAmounts(LesserHeal1, 53, 30);
        bot.SetRankAmounts(LesserHeal2, 84, 45);
        bot.SetRankAmounts(LesserHeal3, 154, 75);
        bot.SetMana(Self, 400, 400);
        bot.SetHealth(Tank, 600 - missing, 600);
        return bot;
    }
}

TEST(SpellBook, ASmallWoundTakesTheLowestRankThatHealsItUp)
{
    FakeBot bot = PriestWithTankMissing(70);

    EXPECT_EQ(Bots::CastableHealRank(bot, LesserHeal1, Tank), LesserHeal2);
}

TEST(SpellBook, AWoundThatNoRankHealsUpTakesTheHighestRank)
{
    FakeBot bot = PriestWithTankMissing(400);

    EXPECT_EQ(Bots::CastableHealRank(bot, LesserHeal1, Tank), LesserHeal3);
}

TEST(SpellBook, TheHealRankKeepsTheTargetLevelRule)
{
    FakeBot bot = PriestWithTankMissing(400);
    bot.SetLevelTooLowFor(LesserHeal3, Tank);

    EXPECT_EQ(Bots::CastableHealRank(bot, LesserHeal1, Tank), LesserHeal2);
}

TEST(SpellBook, TheHealRankSkipsARankThatTheManaDoesNotPay)
{
    FakeBot bot = PriestWithTankMissing(400);
    bot.SetMana(Self, 60, 400);

    EXPECT_EQ(Bots::CastableHealRank(bot, LesserHeal1, Tank), LesserHeal2);
}

TEST(SpellBook, TheHealRankSkipsARankThatTheBotCannotCastNow)
{
    FakeBot bot = PriestWithTankMissing(70);
    bot.SetCannotCast(LesserHeal2);

    EXPECT_EQ(Bots::CastableHealRank(bot, LesserHeal1, Tank), LesserHeal3);
}

TEST(SpellBook, NoHealRankWhileStunned)
{
    FakeBot bot = PriestWithTankMissing(70);
    bot.SetStunned();

    EXPECT_EQ(Bots::CastableHealRank(bot, LesserHeal1, Tank), NoSpell);
}
