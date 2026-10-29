#include "AddonWhisper.h"

#include <algorithm>
#include <cctype>

namespace Bots
{
    bool IsWhisperToSelf(std::string const& sender, std::string const& receiver)
    {
        return sender.size() == receiver.size()
            && std::equal(sender.begin(), sender.end(), receiver.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
    }

    std::string AddonText(char const* prefix, char const* version, std::string const& text)
    {
        return std::string(prefix) + " " + version + ":" + text;
    }
}
