#include "LoginDatabaseInfo.h"

#include <gtest/gtest.h>

// The value of VMANGOS_LOGIN_DATABASE_INFO wins over the config value.
TEST(LoginDatabaseInfo, TheSetVariableWins)
{
    EXPECT_EQ(Bots::ChooseLoginDatabaseInfo("host.docker.internal;3307;mangos;secret;realmd",
                  "database;3306;mangos;mangos;realmd"),
        "host.docker.internal;3307;mangos;secret;realmd");
}

// Without the variable, the config value stays.
TEST(LoginDatabaseInfo, AnUnsetVariableKeepsTheConfigValue)
{
    EXPECT_EQ(Bots::ChooseLoginDatabaseInfo(nullptr, "database;3306;mangos;mangos;realmd"),
        "database;3306;mangos;mangos;realmd");
}

// An empty variable counts as not set.
TEST(LoginDatabaseInfo, AnEmptyVariableKeepsTheConfigValue)
{
    EXPECT_EQ(Bots::ChooseLoginDatabaseInfo("", "database;3306;mangos;mangos;realmd"),
        "database;3306;mangos;mangos;realmd");
}

// The name of the variable is fixed by spec #236.
TEST(LoginDatabaseInfo, TheVariableName)
{
    EXPECT_STREQ(Bots::LoginDatabaseInfoVariable, "VMANGOS_LOGIN_DATABASE_INFO");
}
