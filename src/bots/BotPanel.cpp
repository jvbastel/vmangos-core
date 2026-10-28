#include "BotPanel.h"

#include <algorithm>
#include <cctype>

namespace Bots
{
    namespace
    {
        std::string PanelMessage(std::string const& text)
        {
            return std::string(BotPanelPrefix) + " " + BotPanelVersion + ":" + text;
        }

        bool SameName(std::string const& a, std::string const& b)
        {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y)
                {
                    return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
                });
        }
    }

    std::vector<PanelBot> PanelBotsFor(std::vector<PanelBot> bots, GearFaction faction)
    {
        bots.erase(std::remove_if(bots.begin(), bots.end(), [faction](PanelBot const& bot)
            {
                return bot.worldBot || bot.faction != faction;
            }), bots.end());
        std::sort(bots.begin(), bots.end(), [](PanelBot const& a, PanelBot const& b)
            {
                if (a.role != b.role)
                    return a.role < b.role;
                return a.name < b.name;
            });
        return bots;
    }

    bool IsBotPanelRequest(std::string const& sender, std::string const& receiver, std::string const& text)
    {
        return text == PanelMessage("list") && SameName(sender, receiver);
    }

    std::vector<std::string> BotPanelMessages(std::vector<PanelBot> const& listed)
    {
        std::vector<std::string> messages = {PanelMessage("clear")};
        for (PanelBot const& bot : listed)
            messages.push_back(PanelMessage(std::string("bot:") + RoleName(bot.role) + ":" + bot.name));
        messages.push_back(PanelMessage("end"));
        return messages;
    }
}
