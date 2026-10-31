#ifndef WOWAI_BOTS_LOGIN_DATABASE_INFO_H
#define WOWAI_BOTS_LOGIN_DATABASE_INFO_H

#include <string>

// The login database override of spec jvbastel/WOWAI#236: the play realm and
// the test realm share one login server. The test realm's mangosd then reads
// the play realm's login database, which the environment variable names in
// the format `host;port;user;password;database`. The vmangos-deploy image pins
// `LoginDatabase.Info` in the config, so the override happens where the core
// uses the value: Master::_StartDB and the .service delete characters command.
// It is here, outside the game layer, so that GoogleTest reaches it.
namespace Bots
{
    constexpr char const* LoginDatabaseInfoVariable = "VMANGOS_LOGIN_DATABASE_INFO";

    // The variable's value if it is set and not empty, else the config value.
    std::string ChooseLoginDatabaseInfo(char const* variableValue, std::string const& configValue);

    // ChooseLoginDatabaseInfo with the variable read from the environment.
    std::string LoginDatabaseInfo(std::string const& configValue);
}

#endif
