/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// The GM commands of the WOWAI bots:
//   .wowai bot create <name> <class> <role> [race]
//   .wowai bot login <name>
//   .wowai bot logout <name>
// They work in the game and on the console and SOAP.

#include "BotMgr.h"
#include "BotRecipe.h"
#include "Chat.h"

#include <sstream>

namespace
{
    std::vector<std::string> Words(char const* args)
    {
        std::vector<std::string> words;
        std::istringstream stream(args ? args : "");
        std::string word;
        while (stream >> word)
            words.push_back(word);
        return words;
    }
}

bool ChatHandler::HandleWowaiBotCreateCommand(char* args)
{
    Bots::BotRecipeResult parsed = Bots::ParseBotRecipe(Words(args));
    std::string error = parsed.error;
    if (parsed.ok && sBotMgr.CreateBot(parsed.recipe, error))
    {
        PSendSysMessage("Bot %s made. It logs in now.", parsed.recipe.name.c_str());
        return true;
    }
    SendSysMessage(error.c_str());
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleWowaiBotLoginCommand(char* args)
{
    std::vector<std::string> words = Words(args);
    std::string error = "Usage: .wowai bot login <name>";
    if (words.size() == 1 && sBotMgr.LoginBot(words[0], error))
    {
        PSendSysMessage("Bot %s logs in.", words[0].c_str());
        return true;
    }
    SendSysMessage(error.c_str());
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleWowaiBotLogoutCommand(char* args)
{
    std::vector<std::string> words = Words(args);
    std::string error = "Usage: .wowai bot logout <name>";
    if (words.size() == 1 && sBotMgr.LogoutBot(words[0], error))
    {
        PSendSysMessage("Bot %s logs out.", words[0].c_str());
        return true;
    }
    SendSysMessage(error.c_str());
    SetSentErrorMessage(true);
    return false;
}
