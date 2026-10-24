#include "GroupCommand.h"

#include <cctype>
#include <sstream>
#include <vector>

namespace Bots
{
    namespace
    {
        struct GroupCommandInfo
        {
            GroupCommandKind kind;
            char const* word;
        };

        GroupCommandInfo const GroupCommands[] = {
            {GroupCommandKind::Invite, "invite"},
            {GroupCommandKind::Dismiss, "dismiss"},
        };

        std::string Lower(std::string text)
        {
            for (char& c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }
    }

    ParsedGroupCommand ParseGroupCommand(std::string const& text)
    {
        ParsedGroupCommand parsed = {false, {GroupCommandKind::Invite, ""}};
        std::vector<std::string> words;
        std::istringstream stream(text);
        std::string word;
        while (stream >> word)
            words.push_back(word);
        if (words.empty() || words.size() > 2)
            return parsed;

        std::string const lower = Lower(words[0]);
        for (GroupCommandInfo const& info : GroupCommands)
        {
            if (lower == info.word)
            {
                parsed.ok = true;
                parsed.command.kind = info.kind;
                parsed.command.botName = words.size() == 2 ? words[1] : "";
                return parsed;
            }
        }
        return parsed;
    }
}
