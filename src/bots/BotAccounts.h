#ifndef WOWAI_BOTS_BOT_ACCOUNTS_H
#define WOWAI_BOTS_BOT_ACCOUNTS_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// The pool of bot accounts. Bots use real `realmd` accounts named with the
// prefix of `Bots.AccountPrefix` and a 3-digit number, for example BOT001.
//
// The core keys its sessions by account, so only one character of an account
// can be online at a time. A new bot therefore gets an account of its own
// while numbers are free. Only when all 999 numbers are taken do accounts get
// more bots, up to the realm limit of 10 characters each.
namespace Bots
{
    std::uint32_t const MaxCharactersPerBotAccount = 10;
    int const MaxBotAccountNumber = 999;

    struct BotAccount
    {
        std::string name;
        std::uint32_t characters;
    };

    struct AccountChoice
    {
        enum Kind
        {
            UseExisting, // Put the bot on the account `name`.
            Create,      // Make the account `name`, then put the bot on it.
            PoolFull,    // No account can take one more bot.
        };

        Kind kind;
        std::string name;
    };

    // Chooses the account of a new bot. `accounts` may hold any accounts:
    // only the names of the pool count, in any case.
    //
    // `knownBots`: the number of bots that the bot manager knows on each
    // account, by name in any case. A new character is saved
    // asynchronously, so the database count of its account can still be 0
    // right after the create. An account counts the higher of both numbers,
    // so two creates in a row never share an account.
    AccountChoice ChooseBotAccount(std::string const& prefix, std::vector<BotAccount> const& accounts,
        std::map<std::string, std::uint32_t> const& knownBots = {});

    // The name of the pool account with this number, for example BOT001.
    std::string BotAccountName(std::string const& prefix, int number);
}

#endif
