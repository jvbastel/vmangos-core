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
