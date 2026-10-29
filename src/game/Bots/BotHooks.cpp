/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#include "BotHooks.h"
#include "BotAI.h"
#include "BotCommand.h"
#include "BotMgr.h"
#include "BotPanel.h"
#include "GroupCommand.h"
#include "Group.h"
#include "Player.h"
#include "RoleChoice.h"
#include "Log.h"
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

    // Queues `invite` or `dismiss` for the bot manager. False when the text
    // is no group command for the bots. `whispered` is the name of the
    // whispered bot, or empty for party chat.
    bool RouteGroupCommand(Player* sender, std::string const& whispered, std::string const& text)
    {
        Bots::ParsedGroupCommand const parsed = Bots::ParseGroupCommand(text);
        if (!parsed.ok)
            return false;
        std::string botName = parsed.command.botName;
        // The word alone is for the whispered bot. In party chat it names no bot.
        if (botName.empty())
            botName = whispered;
        if (botName.empty())
            return false;
        sBotMgr.QueueGroupCommand({parsed.command.kind, sender->GetObjectGuid(), botName});
        return true;
    }
}

void BotHooks::OnLevelUp(Player* player)
{
    if (player->IsBot())
        sBotMgr.OnLevelUp(player);
}

void BotHooks::OnPlayerChat(Player* sender, uint32 type, Player* receiver, std::string const& text)
{
    if (!sender)
        return;

    std::vector<ChatBot> bots;
    switch (type)
    {
        case CHAT_MSG_PARTY:
            // Any party chat: the group of the player can have no bot yet.
            if (RouteGroupCommand(sender, "", text))
                return;
            bots = PartyBots(sender);
            break;
        case CHAT_MSG_WHISPER:
            bots = WhisperedBot(sender, receiver);
            if (!bots.empty() && RouteGroupCommand(sender, bots.front().name, text))
                return;
            break;
        default:
            return;
    }
    if (bots.empty())
        return;

    if (RouteBotCommand(sender, type, bots, text))
        return;
    // Not a bot command: one player_message to the chat service, built at
    // the next update of the bot manager on the world thread.
    sBotMgr.Chat().QueuePlayerMessage({sender->GetObjectGuid(), type,
        receiver ? receiver->GetObjectGuid() : ObjectGuid(), text});
}

bool BotHooks::OnWhisperToOffline(Player* sender, std::string const& receiverName, std::string const& text)
{
    // The bot manager answers a name that is no bot.
    return sender && RouteGroupCommand(sender, receiverName, text);
}

// jvbastel/WOWAI#198 and the spec #188 fix-up (S4): the whispers of the
// WowaiRole add-on to the player's own name (bots/AddonWhisper.h): the bot
// panel asks for its list, or the player sends the role choice.
bool BotHooks::OnWhisperToSelf(Player* sender, std::string const& receiverName, std::string const& text)
{
    if (!sender || sender->IsBot())
        return false;
    if (Bots::IsBotPanelRequest(sender->GetName(), receiverName, text))
    {
        sBotMgr.QueueBotPanelRequest(sender->GetObjectGuid());
        return true;
    }
    Bots::RoleChoiceMessage const message = Bots::ParseRoleChoiceWhisper(sender->GetName(), receiverName, text);
    if (!message.ours)
        return false;
    sBotMgr.SetRoleChoice(sender->GetObjectGuid(), message.choice);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Bots] %s sends the role choice \"%s\"", sender->GetName(),
        text.substr(text.find(' ') + 1).c_str());
    return true;
}
