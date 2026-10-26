/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_BOTCHAT_H
#define WOWAI_GAME_BOTCHAT_H

#include "Common.h"
#include "ChatClient.h"
#include "ChatSessions.h"
#include "GroupCoordinator.h"
#include "Inbox.h"
#include "ObjectGuid.h"

#include <memory>
#include <string>
#include <vector>

class Group;
class Player;
struct BotLine;

// What the player wrote to our bots, from the chat hook.
struct PlayerChatMessage
{
    ObjectGuid sender;
    uint32 chatType;     // CHAT_MSG_PARTY or CHAT_MSG_WHISPER.
    ObjectGuid receiver; // The whispered bot.
    std::string text;
};

// A bot chat event that BotChat sent to the chat service, for the result of
// a scripted run.
struct SentChatEvent
{
    uint64 id; // ChatClient::Post
    uint32 groupId;
    Bots::ChatEventKind kind;
    ObjectGuid toBot; // The whispered bot of a player_message.
};

// Bot chat in the game layer: it turns what happens in the game into events
// of the chat service, and the lines of the service into chat of the bots.
// The plain-facts client (src/bots/ChatClient.h) does the HTTP calls on its
// own threads. All of this class runs on the world thread, in the update of
// the bot manager after the maps, except QueuePlayerMessage.
class BotChat
{
    public:
        // The settings `BotChat.*`. At startup and at `.reload config`.
        void LoadConfig();
        // Starts the chat client with the settings of the startup.
        void Start();

        // From any thread (the chat hook runs on the async packet thread):
        // posts one player_message at the next update.
        void QueuePlayerMessage(PlayerChatMessage message) { m_messages.Push(std::move(message)); }

        // Posts the waiting player messages and the session_end events, and
        // gives the lines that the bots say now.
        std::vector<BotLine> Update();

        // A group event of a coordinator: posts `boss_kill`, `wipe`,
        // `loot_roll`, or `rest` for the group, once.
        void PostGroupEvent(uint32 groupId, Bots::GroupEvent const& event);

        // The events that BotChat sent since the last call, and the outcomes
        // of the sent events (ChatClient::TakeOutcomes). The bot manager
        // takes both at each update, so they do not pile up.
        std::vector<SentChatEvent> TakeSent();
        std::vector<Bots::ChatEventOutcome> TakeOutcomes();

        Bots::ChatClientSettings const& GetSettings() const { return m_settings; }

        // A chat event of the group waits for its answer, or a line of the
        // group waits for its delay (Bots::ChatClient::HasPendingChat).
        bool HasPendingChat(uint32 groupId) const { return m_client && m_client->HasPendingChat(groupId); }

    private:
        void PostPlayerMessage(PlayerChatMessage const& message);
        // Posts the event, and keeps it for TakeSent.
        void Post(Bots::ChatEvent const& event, ObjectGuid toBot = ObjectGuid());
        void UpdateSessions();
        bool MaySay(Bots::DueChatLine const& due, BotLine& line) const;

        Bots::ChatClientSettings m_settings;
        std::unique_ptr<Bots::ChatClient> m_client;
        Bots::Inbox<PlayerChatMessage> m_messages;
        Bots::ChatSessions m_sessions;
        int64 m_lastSessionCheck = 0;
        std::vector<SentChatEvent> m_sent;
};

#endif
