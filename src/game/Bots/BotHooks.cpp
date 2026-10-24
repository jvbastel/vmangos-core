/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "BotHooks.h"
#include "BotAI.h"
#include "BotCommand.h"
#include "Group.h"
#include "Player.h"
#include "SharedDefines.h"

#include <vector>

namespace
{
    struct ChatBot
    {
        std::string name;
        BotAI* ai;
    };

    // The WOWAI bots in the party of the sender that hear its party chat.
    std::vector<ChatBot> PartyBots(Player* sender)
    {
        std::vector<ChatBot> bots;
        // Like the party branch of the core: the original group first.
        Group* group = sender->GetOriginalGroup();
        if (!group)
            group = sender->GetGroup();
        if (!group)
            return bots;

        uint8 const subGroup = group->GetMemberGroup(sender->GetObjectGuid());
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (!member || member == sender || ref->getSubGroup() != subGroup)
                continue;
            if (BotAI* ai = BotAI::Of(member))
                bots.push_back({member->GetName(), ai});
        }
        return bots;
    }

    // The whispered player, if it is a WOWAI bot.
    std::vector<ChatBot> WhisperedBot(Player* sender, Player* receiver)
    {
        std::vector<ChatBot> bots;
        if (receiver && receiver != sender)
            if (BotAI* ai = BotAI::Of(receiver))
                bots.push_back({receiver->GetName(), ai});
        return bots;
    }

    // Pushes a bot command into the inboxes of the bots it addresses. False
    // when the text is no bot command.
    bool RouteBotCommand(Player* sender, uint32 type, std::vector<ChatBot> const& bots, std::string const& text)
    {
        std::vector<std::string> names;
        for (ChatBot const& bot : bots)
            names.push_back(bot.name);

        Bots::ParsedBotCommand const parsed = Bots::ParseBotCommand(text, names);
        if (!parsed.ok)
            return false;

        for (std::string const& name : Bots::AddressedBots(parsed.command, names))
            for (ChatBot const& bot : bots)
                if (bot.name == name)
                    bot.ai->PushCommand({parsed.command.kind, sender->GetObjectGuid(), type});
        return true;
    }
}

void BotHooks::OnPlayerChat(Player* sender, uint32 type, Player* receiver, std::string const& text)
{
    if (!sender)
        return;

    std::vector<ChatBot> bots;
    switch (type)
    {
        case CHAT_MSG_PARTY:
            bots = PartyBots(sender);
            break;
        case CHAT_MSG_WHISPER:
            bots = WhisperedBot(sender, receiver);
            break;
        default:
            return;
    }
    if (bots.empty())
        return;

    if (RouteBotCommand(sender, type, bots, text))
        return;
    // Not a bot command: the chat client posts it to the chat service in a
    // later ticket (one player_message).
}
