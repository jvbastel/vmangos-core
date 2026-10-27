#ifndef WOWAI_BOTS_CHAT_CLIENT_H
#define WOWAI_BOTS_CHAT_CLIENT_H

#include "ChatContract.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The chat client of the core: it sends bot chat events to the chat service
// and holds the lines until their delay ends.
//
// Threads: the bot manager calls Post and TakeDue on the world thread. A fixed
// pool of worker threads does the HTTP calls. The workers see only the plain
// facts of ChatEvent and never a game object: this library has no game
// header (ADR 0002).
namespace Bots
{
    // The settings `BotChat.*` of mangosd.conf.
    struct ChatClientSettings
    {
        bool enable = true;
        std::string url = "http://botchat:8000";
        unsigned workers = 4;        // The 4 slots of llama-server.
        std::size_t queueSize = 64;  // Requests that wait for a worker.
        unsigned timeoutMs = 5000;   // One HTTP call.
    };

    // A line that comes later than this, compared with the earliest time it
    // could have come (the event time plus its delay), drops: the talk has
    // moved on.
    std::int64_t const MaxChatLineLateMs = 10000;

    // The result of one POST to the service. `answered` is false when the
    // service did not answer (down, timeout), with the reason in `error`.
    struct ChatPostResult
    {
        bool answered = false;
        int status = 0;
        std::string body;
        std::string error;
    };

    // A kept connection to the chat service. Each worker has its own.
    class ChatConnection
    {
    public:
        virtual ~ChatConnection() = default;
        // `path` is ChatEventsPath or ChatRosterPath.
        virtual ChatPostResult Post(std::string const& path, std::string const& body) = 0;
    };

    using ChatConnectionFactory = std::function<std::unique_ptr<ChatConnection>()>;

    // An HTTP connection with cpp-httplib and keep-alive (ChatHttp.cpp).
    std::unique_ptr<ChatConnection> NewHttpChatConnection(ChatClientSettings const& settings);

    // A line whose delay ended, with the event facts that the bot manager
    // needs to check that the bot may still say it.
    struct DueChatLine
    {
        ChatLine line;
        std::uint32_t groupId = 0;
        Guid playerGuid = 0;
        // The event of the answer, as Post gave it.
        std::uint64_t eventId = 0;
        ChatEventKind eventKind = ChatEventKind::PlayerMessage;
    };

    // Whether a posted event reached the chat service: it answered with
    // lines (none is fine). False when the service was down, rejected it, or
    // gave a broken answer, and when the full queue dropped it.
    struct ChatEventOutcome
    {
        std::uint64_t eventId = 0;
        bool reached = false;
    };

    class ChatClient
    {
    public:
        // Milliseconds of a steady clock.
        using Clock = std::function<std::int64_t()>;
        // Called from the worker threads too.
        using Logger = std::function<void(std::string const&)>;

        // Starts the workers. With no factory, they use HTTP.
        ChatClient(ChatClientSettings const& settings, Clock clock, Logger log, ChatConnectionFactory connect = nullptr);
        // Stops the workers. A request that a worker sends ends first (at
        // most the HTTP timeout). Queued requests are not sent.
        ~ChatClient();

        ChatClient(ChatClient const&) = delete;
        ChatClient& operator=(ChatClient const&) = delete;

        // Copies the event onto the request queue. When the queue is full,
        // the oldest request drops. The id of the event (never 0), for its
        // lines and its outcome.
        std::uint64_t Post(ChatEvent const& event);

        // The lines whose delay ended at `nowMs`, in the order of their due
        // time, and in speaking order within one answer. Lines that are more
        // than MaxChatLineLateMs late drop.
        std::vector<DueChatLine> TakeDue(std::int64_t nowMs);

        // The outcome of each event that the client is done with, since the
        // last call.
        std::vector<ChatEventOutcome> TakeOutcomes();

        // The GUIDs of all bot characters. The first call makes a worker send
        // them to the service (POST /v1/roster), before the next event. They
        // go again before the next call after a call with no answer: the
        // service was down or restarted, for example. No event goes while the
        // roster is sent. A later call only
        // keeps the newest list for that. An answer that is not 200 (an older
        // service) logs once and is not sent again.
        void SetRoster(std::vector<Guid> botGuids);

        // Requests that wait or that a worker sends, and a roster that is due.
        std::size_t Pending() const;

        // A request of the group waits or is sent, or a line of the group
        // waits for its delay. A line that TakeDue gave or dropped is not
        // pending.
        bool HasPendingChat(std::uint32_t groupId) const;

    private:
        struct Request
        {
            ChatEvent event;
            std::int64_t postedAt;
            std::uint64_t id;
        };

        struct HeldLine
        {
            DueChatLine due;
            std::int64_t dueAt;
            std::int64_t earliestAt; // postedAt + delay
            std::uint64_t order;
        };

        void Work(std::unique_ptr<ChatConnection> connection);
        void Handle(ChatConnection& connection, Request const& request);
        void SendRoster(ChatConnection& connection, std::vector<Guid> const& botGuids);

        // None: no roster yet. Due: a worker sends it now. Sending: a worker
        // sends it. Sent: the service has it. Retry: it goes before the next
        // event (no answer to the last call).
        enum class RosterState { None, Due, Sending, Sent, Retry };
        void Log(std::string const& text);

        ChatClientSettings m_settings;
        Clock m_clock;
        Logger m_log;

        mutable std::mutex m_mutex;
        std::condition_variable m_wake;
        std::deque<Request> m_requests;
        std::vector<std::uint32_t> m_inFlight; // The group of each sent request.
        bool m_stopping = false;
        std::vector<HeldLine> m_lines;
        std::uint64_t m_nextOrder = 0;
        std::uint64_t m_nextEventId = 1;
        std::vector<ChatEventOutcome> m_outcomes;
        std::vector<Guid> m_roster;
        RosterState m_rosterState = RosterState::None;

        std::vector<std::thread> m_workers;
    };
}

#endif
