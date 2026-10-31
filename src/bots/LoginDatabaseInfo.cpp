#include "LoginDatabaseInfo.h"

#include <cstdlib>

namespace Bots
{
    std::string ChooseLoginDatabaseInfo(char const* variableValue, std::string const& configValue)
    {
        if (variableValue && *variableValue)
            return variableValue;
        return configValue;
    }

    std::string LoginDatabaseInfo(std::string const& configValue)
    {
        return ChooseLoginDatabaseInfo(std::getenv(LoginDatabaseInfoVariable), configValue);
    }
}
