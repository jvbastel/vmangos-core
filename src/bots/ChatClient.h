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

    // The result of one POST /v1/events. `answered` is false when the
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
        virtual ChatPostResult Post(std::string const& body) = 0;
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
        // the oldest request drops.
        void Post(ChatEvent const& event);

        // The lines whose delay ended at `nowMs`, in the order of their due
        // time, and in speaking order within one answer. Lines that are more
        // than MaxChatLineLateMs late drop.
        std::vector<DueChatLine> TakeDue(std::int64_t nowMs);

        // Requests that wait or that a worker sends.
        std::size_t Pending() const;

    private:
        struct Request
        {
            ChatEvent event;
            std::int64_t postedAt;
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
        void Log(std::string const& text);

        ChatClientSettings m_settings;
        Clock m_clock;
        Logger m_log;

        mutable std::mutex m_mutex;
        std::condition_variable m_wake;
        std::deque<Request> m_requests;
        std::size_t m_inFlight = 0;
        bool m_stopping = false;
        std::vector<HeldLine> m_lines;
        std::uint64_t m_nextOrder = 0;

        std::vector<std::thread> m_workers;
    };
}

#endif
