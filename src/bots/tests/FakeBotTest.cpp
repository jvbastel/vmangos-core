#include "FakeBot.h"
#include "ExtraPackWatch.h"

#include <gtest/gtest.h>

using Bots::BotActions;
using Bots::BotView;
using Bots::Guid;

namespace
{
    Guid const Self = 0x0000000000000011;
    Guid const Kobold = 0xF130000302000022;
    std::uint32_t const Fireball = 133;
}

TEST(FakeBot, AnswersQueriesAboutUnitsByGuid)
{
    Bots::Tests::FakeBot fake(Self);
    fake.SetHealth(Self, 300, 1000);
    fake.SetCurrentCast(Kobold, Fireball);

    BotView const& view = fake;
    EXPECT_EQ(view.GetSelf(), Self);
    EXPECT_EQ(view.GetHealth(Self).current, 300u);
    EXPECT_EQ(view.GetHealth(Self).max, 1000u);
    EXPECT_EQ(view.GetCurrentCast(Kobold), Fireball);
    EXPECT_EQ(view.GetCurrentCast(Self), Bots::NoSpell);
}

TEST(FakeBot, RecordsDeedsInOrder)
{
    Bots::Tests::FakeBot fake(Self);

    BotActions& actions = fake;
    actions.CastSpell(Fireball, Kobold);
    actions.MoveTo(Bots::Position{-10.5f, 20.0f, 3.25f});

    EXPECT_EQ(fake.Deeds(), (std::vector<std::string>{
        "cast 133 on 0xf130000302000022",
        "move to -10.5 20 3.25",
    }));
}

// The warn hook of jvbastel/WOWAI#105 (bots/ExtraPackWatch.h).
TEST(FakeBot, RecordsAnExtraPackWarn)
{
    Bots::Tests::FakeBot fake(Self);

    Bots::WarnExtraPacks(fake, 2);

    EXPECT_EQ(fake.Deeds(), (std::vector<std::string>{"warn Careful: this pull brings 2 more packs."}));
}
