#include "BotCommand.h"

#include <cctype>
#include <sstream>

namespace Bots
{
    namespace
    {
        struct CommandInfo
        {
            BotCommandKind kind;
            char const* word;
        };

        CommandInfo const Commands[] = {
            {BotCommandKind::Follow, "follow"},
            {BotCommandKind::Stay, "stay"},
            {BotCommandKind::Come, "come"},
            {BotCommandKind::Stop, "stop"},
            {BotCommandKind::Pull, "pull"},
        };

        std::string Lower(std::string text)
        {
            for (char& c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        std::vector<std::string> SplitWords(std::string const& text)
        {
            std::vector<std::string> words;
            std::istringstream stream(text);
            std::string word;
            while (stream >> word)
                words.push_back(word);
            return words;
        }

        bool FindCommand(std::string const& word, BotCommandKind& kind)
        {
            std::string const lower = Lower(word);
            for (CommandInfo const& info : Commands)
            {
                if (lower == info.word)
                {
                    kind = info.kind;
                    return true;
                }
            }
            return false;
        }

        bool FindBotName(std::string const& word, std::vector<std::string> const& botNames, std::string& name)
        {
            std::string const lower = Lower(word);
            for (std::string const& botName : botNames)
            {
                if (Lower(botName) == lower)
                {
                    name = botName;
                    return true;
                }
            }
            return false;
        }
    }

    ParsedBotCommand ParseBotCommand(std::string const& text, std::vector<std::string> const& botNames)
    {
        ParsedBotCommand parsed = {false, {BotCommandKind::Stop, ""}};
        std::vector<std::string> const words = SplitWords(text);
        if (words.empty() || words.size() > 2)
            return parsed;

        std::string botName;
        if (words.size() == 2 && !FindBotName(words[0], botNames, botName))
            return parsed;
        if (!FindCommand(words.back(), parsed.command.kind))
            return parsed;

        parsed.ok = true;
        parsed.command.botName = botName;
        return parsed;
    }

    std::vector<std::string> AddressedBots(BotCommand const& command, std::vector<std::string> const& botNames)
    {
        if (command.botName.empty())
            return botNames;
        std::vector<std::string> addressed;
        for (std::string const& name : botNames)
            if (name == command.botName)
                addressed.push_back(name);
        return addressed;
    }

    char const* BotCommandWord(BotCommandKind kind)
    {
        for (CommandInfo const& info : Commands)
            if (info.kind == kind)
                return info.word;
        return "";
    }
}
