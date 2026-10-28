#include "BotPanel.h"

#include <algorithm>

namespace Bots
{
    namespace
    {
        std::string PanelMessage(std::string const& text)
        {
            return std::string(BotPanelPrefix) + "\t" + BotPanelVersion + ":" + text;
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

    bool IsBotPanelRequest(std::string const& message)
    {
        return message == PanelMessage("list");
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
