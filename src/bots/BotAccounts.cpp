#include "BotAccounts.h"

#include <cctype>
#include <cstdio>
#include <set>

namespace Bots
{
    namespace
    {
        std::string Upper(std::string text)
        {
            for (char& c : text)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return text;
        }

        // The number of a pool account name, or 0 for any other name.
        int PoolNumber(std::string const& upperPrefix, std::string const& name)
        {
            std::string upperName = Upper(name);
            if (upperName.size() != upperPrefix.size() + 3 || upperName.compare(0, upperPrefix.size(), upperPrefix) != 0)
                return 0;
            int number = 0;
            for (std::size_t i = upperPrefix.size(); i < upperName.size(); ++i)
            {
                if (!std::isdigit(static_cast<unsigned char>(upperName[i])))
                    return 0;
                number = number * 10 + (upperName[i] - '0');
            }
            return number;
        }
    }

    std::string BotAccountName(std::string const& prefix, int number)
    {
        char digits[8];
        std::snprintf(digits, sizeof(digits), "%03d", number);
        return Upper(prefix) + digits;
    }

    AccountChoice ChooseBotAccount(std::string const& prefix, std::vector<BotAccount> const& accounts)
    {
        std::string const upperPrefix = Upper(prefix);
        std::set<int> taken;
        BotAccount const* emptiest = nullptr;
        int emptiestNumber = 0;
        for (BotAccount const& account : accounts)
        {
            int number = PoolNumber(upperPrefix, account.name);
            if (number == 0)
                continue;
            taken.insert(number);
            if (!emptiest || account.characters < emptiest->characters ||
                (account.characters == emptiest->characters && number < emptiestNumber))
            {
                emptiest = &account;
                emptiestNumber = number;
            }
        }

        if (emptiest && emptiest->characters == 0)
            return {AccountChoice::UseExisting, Upper(emptiest->name)};

        for (int number = 1; number <= MaxBotAccountNumber; ++number)
            if (taken.find(number) == taken.end())
                return {AccountChoice::Create, BotAccountName(prefix, number)};

        if (emptiest && emptiest->characters < MaxCharactersPerBotAccount)
            return {AccountChoice::UseExisting, Upper(emptiest->name)};

        return {AccountChoice::PoolFull, std::string()};
    }
}
