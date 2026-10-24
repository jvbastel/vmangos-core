#include "BotAccounts.h"

#include <gtest/gtest.h>

#include <cstdio>

using Bots::AccountChoice;
using Bots::BotAccount;
using Bots::ChooseBotAccount;

namespace
{
    // All 999 accounts of the pool, each with the same number of characters.
    std::vector<BotAccount> FullNumberRange(std::uint32_t characters)
    {
        std::vector<BotAccount> accounts;
        for (int number = 1; number <= 999; ++number)
        {
            char name[8];
            std::snprintf(name, sizeof(name), "BOT%03d", number);
            accounts.push_back({name, characters});
        }
        return accounts;
    }
}

TEST(BotAccounts, AnEmptyPoolMakesTheFirstAccount)
{
    AccountChoice choice = ChooseBotAccount("BOT", {});

    EXPECT_EQ(choice.kind, AccountChoice::Create);
    EXPECT_EQ(choice.name, "BOT001");
}

// Only one character of an account can be online at a time, so each new bot
// first gets an account of its own.
TEST(BotAccounts, ANewBotGetsANewAccountWhileNumbersAreFree)
{
    AccountChoice choice = ChooseBotAccount("BOT", {{"BOT001", 1}, {"BOT002", 1}});

    EXPECT_EQ(choice.kind, AccountChoice::Create);
    EXPECT_EQ(choice.name, "BOT003");
}

TEST(BotAccounts, AnAccountWithoutCharactersIsUsedFirst)
{
    AccountChoice choice = ChooseBotAccount("BOT", {{"BOT001", 1}, {"BOT002", 0}});

    EXPECT_EQ(choice.kind, AccountChoice::UseExisting);
    EXPECT_EQ(choice.name, "BOT002");
}

TEST(BotAccounts, ANewAccountTakesTheLowestFreeNumber)
{
    AccountChoice choice = ChooseBotAccount("BOT", {{"BOT001", 1}, {"BOT003", 2}});

    EXPECT_EQ(choice.kind, AccountChoice::Create);
    EXPECT_EQ(choice.name, "BOT002");
}

TEST(BotAccounts, AccountsOutsideThePoolDoNotCount)
{
    AccountChoice choice = ChooseBotAccount("BOT", {{"BOTANIST", 0}, {"BOT1", 0}, {"GM", 0}, {"XBOT001", 0}});

    EXPECT_EQ(choice.kind, AccountChoice::Create);
    EXPECT_EQ(choice.name, "BOT001");
}

TEST(BotAccounts, ThePrefixIgnoresCase)
{
    AccountChoice choice = ChooseBotAccount("bot", {{"BOT001", 0}});

    EXPECT_EQ(choice.kind, AccountChoice::UseExisting);
    EXPECT_EQ(choice.name, "BOT001");
}

TEST(BotAccounts, WhenAllNumbersAreTakenTheEmptiestAccountGetsTheBot)
{
    std::vector<BotAccount> accounts = FullNumberRange(10);
    accounts[6].characters = 9;   // BOT007
    accounts[499].characters = 3; // BOT500

    AccountChoice choice = ChooseBotAccount("BOT", accounts);

    EXPECT_EQ(choice.kind, AccountChoice::UseExisting);
    EXPECT_EQ(choice.name, "BOT500");
}

TEST(BotAccounts, ThePoolIsFullWhenEachAccountHas10Characters)
{
    AccountChoice choice = ChooseBotAccount("BOT", FullNumberRange(10));

    EXPECT_EQ(choice.kind, AccountChoice::PoolFull);
}
