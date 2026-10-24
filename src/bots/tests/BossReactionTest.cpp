#include "BossReactions.h"
#include "ClassModules.h"
#include "FakeBot.h"

#include <gtest/gtest.h>

#include <algorithm>

using Bots::BossReactionSlot;
using Bots::BotRole;
using Bots::GroupCoordinator;
using Bots::NoGuid;
using Bots::StrategyStack;
using Bots::Tests::FakeBot;

namespace
{
    Bots::Guid const Tank = 0x0000000000000011;
    Bots::Guid const Healer = 0x0000000000000012;
    Bots::Guid const Mage = 0x0000000000000013;
    Bots::Guid const Taragaman = 0xF130002CF0000044;
    Bots::Guid const Imp = 0xF130002BC4000055;
    std::uint32_t const TaragamanEntry = 11520;
    std::uint32_t const ImpEntry = 11196;

    bool Has(StrategyStack const& stack, std::string const& name)
    {
        std::vector<std::string> const names = stack.Names();
        return std::find(names.begin(), names.end(), name) != names.end();
    }

    // A healer in a group with a tank and a mage, near Taragaman.
    struct HealerBot
    {
        FakeBot fake{Healer};
        GroupCoordinator group;
        StrategyStack stack = Bots::MakeStrategyStack(Bots::BotClass::Priest, BotRole::Healer);
        BossReactionSlot slot;
        std::size_t const baseSize = stack.Names().size();

        HealerBot()
        {
            fake.SetGroupMembers({Tank, Mage});
            for (Bots::Guid member : {Healer, Tank, Mage})
                fake.SetHealth(member, 600, 600);
            fake.SetHealth(Taragaman, 5000, 5000);
            fake.SetEntry(Taragaman, TaragamanEntry);
            fake.SetHealth(Imp, 300, 300);
            fake.SetEntry(Imp, ImpEntry);
        }

        BossReactionSlot::Change Update() { return slot.Update(fake, group, stack, BotRole::Healer); }
    };
}

TEST(BossReactions, TaragamanHasABossReactionForEachRole)
{
    for (BotRole role : {BotRole::Tank, BotRole::Healer, BotRole::Melee, BotRole::Ranged})
    {
        auto const reaction = Bots::MakeBossReaction(TaragamanEntry, role);
        ASSERT_NE(reaction, nullptr);
        EXPECT_EQ(reaction->name, "taragaman the hungerer");
    }
}

TEST(BossReactions, ACreatureWithoutABossReactionHasNone)
{
    EXPECT_EQ(Bots::MakeBossReaction(ImpEntry, BotRole::Tank), nullptr);
    EXPECT_FALSE(Bots::HasBossReaction(ImpEntry));
    EXPECT_TRUE(Bots::HasBossReaction(TaragamanEntry));
}

TEST(BossReactionSlot, NoBossNoReaction)
{
    HealerBot bot;
    bot.fake.SetVictim(Imp, Tank);

    EXPECT_EQ(bot.Update(), BossReactionSlot::Change::None);
    EXPECT_EQ(bot.group.GetBossFight().boss, NoGuid);
    EXPECT_FALSE(Has(bot.stack, "taragaman the hungerer"));
}

TEST(BossReactionSlot, TheBossAttacksAGroupMemberThenTheReactionIsPushed)
{
    HealerBot bot;
    bot.fake.SetVictim(Taragaman, Tank);

    EXPECT_EQ(bot.Update(), BossReactionSlot::Change::Pushed);

    EXPECT_EQ(bot.group.GetBossFight().boss, Taragaman);
    EXPECT_EQ(bot.group.GetBossFight().entry, TaragamanEntry);
    EXPECT_TRUE(Has(bot.stack, "taragaman the hungerer"));
    EXPECT_EQ(bot.slot.Active(), "taragaman the hungerer");
    // Once only.
    EXPECT_EQ(bot.Update(), BossReactionSlot::Change::None);
    EXPECT_EQ(bot.stack.Names().size(), bot.baseSize + 1);
}

TEST(BossReactionSlot, AGroupMemberAttacksTheBossThenTheReactionIsPushed)
{
    HealerBot bot;
    bot.fake.SetVictim(Mage, Taragaman);
    bot.fake.SetInCombat(Taragaman);

    EXPECT_EQ(bot.Update(), BossReactionSlot::Change::Pushed);
    EXPECT_TRUE(Has(bot.stack, "taragaman the hungerer"));
}

TEST(BossReactionSlot, ABossFightThatAnotherBotStartedPushesTheReaction)
{
    HealerBot bot;
    bot.group.StartBossFight(Taragaman, TaragamanEntry);
    bot.fake.SetInCombat(Taragaman);

    EXPECT_EQ(bot.Update(), BossReactionSlot::Change::Pushed);
    EXPECT_TRUE(Has(bot.stack, "taragaman the hungerer"));
}

TEST(BossReactionSlot, TheBossDiesThenTheReactionIsPopped)
{
    HealerBot bot;
    bot.fake.SetVictim(Taragaman, Tank);
    bot.Update();

    bot.fake.SetHealth(Taragaman, 0, 5000);
    bot.fake.SetVictim(Taragaman, NoGuid);

    EXPECT_EQ(bot.Update(), BossReactionSlot::Change::Popped);
    EXPECT_EQ(bot.group.GetBossFight().boss, NoGuid);
    EXPECT_FALSE(Has(bot.stack, "taragaman the hungerer"));
    EXPECT_EQ(bot.slot.Active(), "");
    EXPECT_EQ(bot.stack.Names().size(), bot.baseSize);
}

TEST(BossReactionSlot, TheBossLeavesTheFightThenTheReactionIsPopped)
{
    HealerBot bot;
    bot.fake.SetVictim(Taragaman, Tank);
    bot.Update();

    // A wipe: the boss lives and goes back out of combat.
    bot.fake.SetVictim(Taragaman, NoGuid);

    EXPECT_EQ(bot.Update(), BossReactionSlot::Change::Popped);
    EXPECT_FALSE(Has(bot.stack, "taragaman the hungerer"));
}

TEST(BossReactionSlot, ABotThatCannotSeeTheBossKeepsTheFight)
{
    HealerBot bot;
    bot.group.StartBossFight(0xF130002CF0000099, TaragamanEntry);

    EXPECT_EQ(bot.Update(), BossReactionSlot::Change::Pushed);
    EXPECT_EQ(bot.group.GetBossFight().boss, 0xF130002CF0000099u);
}

TEST(BossReactionSlot, AFightThatAnotherBotEndedPopsTheReaction)
{
    HealerBot bot;
    bot.fake.SetVictim(Taragaman, Tank);
    bot.Update();

    bot.group.EndBossFight(Taragaman);
    bot.fake.SetVictim(Taragaman, NoGuid);

    EXPECT_EQ(bot.Update(), BossReactionSlot::Change::Popped);
    EXPECT_FALSE(Has(bot.stack, "taragaman the hungerer"));
}

TEST(BossReactionSlot, AWipeAlsoEndsTheFocusOnTheBoss)
{
    HealerBot bot;
    bot.fake.SetVictim(Taragaman, Tank);
    bot.group.SetFocusTarget(Taragaman);
    bot.Update();

    // The group died, and he went back out of combat: the damage bots must
    // not attack him again on their own after they revive.
    bot.fake.SetVictim(Taragaman, NoGuid);
    bot.Update();

    EXPECT_EQ(bot.group.GetFocusTarget(), NoGuid);
}

TEST(BossReactionSlot, TheEndOfTheFightKeepsAnotherFocus)
{
    HealerBot bot;
    bot.fake.SetVictim(Taragaman, Tank);
    bot.Update();
    bot.group.SetFocusTarget(Imp);

    bot.fake.SetVictim(Taragaman, NoGuid);
    bot.Update();

    EXPECT_EQ(bot.group.GetFocusTarget(), Imp);
}
