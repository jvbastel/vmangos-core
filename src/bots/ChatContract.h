#ifndef WOWAI_BOTS_CHAT_CONTRACT_H
#define WOWAI_BOTS_CHAT_CONTRACT_H

#include "BotTypes.h"

#include <cstdint>
#include <string>
#include <vector>

// The bot chat event contract: the C++ mirror of the JSON schema of the chat
// service (botchat repository, src/botchat/schema/event.v1.json,
// response.v1.json, and roster.v1.json). The core sends one event with plain facts only, never
// prompt text, and gets the lines of the speakers back.
//
// A schema change raises ChatContractVersion. This header and the service
// change as a pair, and the service release goes first: the service accepts
// the current and the previous version.
namespace Bots
{
    int const ChatContractVersion = 1;

    enum class ChatEventKind
    {
        PlayerMessage,
        BossKill,
        Wipe,
        LootRoll,
        Rest,
        SessionEnd,
    };

    enum class ChatChannel
    {
        Party,
        Whisper,
    };

    enum class SessionEndReason
    {
        BotLeft,
        GroupDisbanded,
        PlayerLogout,
    };

    struct ChatPlayer
    {
        Guid guid = 0;
        std::string name;
    };

    // A bot of the group. Race and class are lowercase words ("orc",
    // "warrior"). The role is "tank", "healer", or "dps".
    struct ChatEventBot
    {
        Guid guid = 0;
        std::string name;
        std::string race;
        std::string botClass;
        std::string role;
    };

    // player_message: what the player wrote, and where.
    struct ChatMessage
    {
        ChatChannel channel = ChatChannel::Party;
        std::string text;
        Guid toBotGuid = 0; // Only for a whisper: the bot that got it.
    };

    // One event. Each kind uses only its own facts:
    // - player_message: message.
    // - boss_kill: bossName. wipe: bossName if the fight had a boss.
    // - loot_roll: itemName and winnerGuid (a bot or the player).
    // - rest: restingBotGuid.
    // - session_end: endedBotGuids and reason.
    struct ChatEvent
    {
        ChatEventKind kind = ChatEventKind::PlayerMessage;
        std::uint32_t groupId = 0; // The ID of the core's Group.
        ChatPlayer player;
        std::vector<ChatEventBot> bots; // The bots of the group, at least one.
        std::string zone;

        ChatMessage message;
        std::string bossName;
        std::string itemName;
        Guid winnerGuid = 0;
        Guid restingBotGuid = 0;
        std::vector<Guid> endedBotGuids;
        SessionEndReason reason = SessionEndReason::BotLeft;
    };

    // A line of a speaker. The delay counts from when the response arrives.
    struct ChatLine
    {
        Guid botGuid = 0;
        ChatChannel channel = ChatChannel::Party;
        std::string text;
        std::uint32_t delayMs = 0;
    };

    // The lines in the order that the bots speak.
    struct ChatResponse
    {
        int contractVersion = 0;
        std::vector<ChatLine> lines;
    };

    // The name of the event in the contract, for example "boss_kill".
    char const* ChatEventName(ChatEventKind kind);
    // The routes of the chat service.
    char const* const ChatEventsPath = "/v1/events";
    char const* const ChatRosterPath = "/v1/roster";

    // The body of POST /v1/events, with ChatContractVersion.
    std::string EncodeChatEvent(ChatEvent const& event);

    // The body of POST /v1/roster: the GUIDs of all bot characters. The
    // service deletes the memory (persona, log, summaries, chat sessions) of
    // every other bot GUID that it holds. An empty roster deletes nothing.
    std::string EncodeChatRoster(std::vector<Guid> const& botGuids);

    // The body of a 200 answer to a roster: the GUIDs whose memory the
    // service deleted. False with a short reason in `error` otherwise.
    bool DecodeChatRosterAnswer(std::string const& body, std::vector<Guid>& deleted, std::string& error);

    // The body of a 200 answer. False with a short reason in `error` when the
    // body does not match the schema or has another contract version.
    bool DecodeChatResponse(std::string const& body, ChatResponse& response, std::string& error);

    // A log text for an answer that is not 200, for example a rejected
    // contract version (HTTP 400 with the accepted versions).
    std::string DescribeChatServiceError(int status, std::string const& body);
}

#endif
