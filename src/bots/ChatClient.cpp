#include "ChatClient.h"

#include <algorithm>

namespace
{
    char const* EventName(Bots::ChatEventKind kind)
    {
        switch (kind)
        {
            case Bots::ChatEventKind::PlayerMessage: return "player_message";
            case Bots::ChatEventKind::BossKill: return "boss_kill";
            case Bots::ChatEventKind::Wipe: return "wipe";
            case Bots::ChatEventKind::LootRoll: return "loot_roll";
            case Bots::ChatEventKind::Rest: return "rest";
            case Bots::ChatEventKind::SessionEnd: break;
        }
        return "session_end";
    }

    std::string Describe(Bots::ChatEvent const& event)
    {
        return std::string(EventName(event.kind)) + " of group " + std::to_string(event.groupId);
    }
}

Bots::ChatClient::ChatClient(ChatClientSettings const& settings, Clock clock, Logger log, ChatConnectionFactory connect)
    : m_settings(settings), m_clock(std::move(clock)), m_log(std::move(log))
{
    if (!connect)
        connect = [settings]() { return NewHttpChatConnection(settings); };
    unsigned const workers = std::max(1u, m_settings.workers);
    for (unsigned i = 0; i < workers; ++i)
        m_workers.emplace_back([this, connect]() { Work(connect()); });
}

Bots::ChatClient::~ChatClient()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
        m_requests.clear();
    }
    m_wake.notify_all();
    for (std::thread& worker : m_workers)
        worker.join();
}

void Bots::ChatClient::Post(ChatEvent const& event)
{
    std::string dropped;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping)
            return;
        if (m_requests.size() >= std::max<std::size_t>(1, m_settings.queueSize))
        {
            dropped = Describe(m_requests.front().event);
            m_requests.pop_front();
        }
        m_requests.push_back({event, m_clock()});
    }
    m_wake.notify_one();
    if (!dropped.empty())
        Log("the request queue is full: dropped the oldest event, " + dropped);
}

std::vector<Bots::DueChatLine> Bots::ChatClient::TakeDue(std::int64_t nowMs)
{
    std::vector<HeldLine> due;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto const firstLater = std::stable_partition(m_lines.begin(), m_lines.end(),
            [nowMs](HeldLine const& line) { return line.dueAt <= nowMs; });
        due.assign(m_lines.begin(), firstLater);
        m_lines.erase(m_lines.begin(), firstLater);
    }
    std::sort(due.begin(), due.end(), [](HeldLine const& a, HeldLine const& b) {
        return a.dueAt != b.dueAt ? a.dueAt < b.dueAt : a.order < b.order;
    });

    std::vector<DueChatLine> lines;
    for (HeldLine const& held : due)
    {
        std::int64_t const late = nowMs - held.earliestAt;
        if (late > MaxChatLineLateMs)
        {
            Log("dropped a line of bot " + std::to_string(held.due.line.botGuid) + ": " +
                std::to_string(late) + " ms late");
            continue;
        }
        lines.push_back(held.due);
    }
    return lines;
}

void Bots::ChatClient::SetRoster(std::vector<Guid> botGuids)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_roster = std::move(botGuids);
        if (m_rosterState == RosterState::None)
            m_rosterState = RosterState::Due;
    }
    m_wake.notify_one();
}

std::size_t Bots::ChatClient::Pending() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    bool const roster = m_rosterState == RosterState::Due || m_rosterState == RosterState::Sending;
    return m_requests.size() + m_inFlight.size() + (roster ? 1 : 0);
}

bool Bots::ChatClient::HasPendingChat(std::uint32_t groupId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::any_of(m_requests.begin(), m_requests.end(),
               [groupId](Request const& request) { return request.event.groupId == groupId; }) ||
        std::find(m_inFlight.begin(), m_inFlight.end(), groupId) != m_inFlight.end() ||
        std::any_of(m_lines.begin(), m_lines.end(),
            [groupId](HeldLine const& line) { return line.due.groupId == groupId; });
}

void Bots::ChatClient::Work(std::unique_ptr<ChatConnection> connection)
{
    for (;;)
    {
        Request request;
        std::vector<Guid> roster;
        bool sendRoster = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this]() {
                return m_stopping || !m_requests.empty() || m_rosterState == RosterState::Due;
            });
            if (m_stopping)
                return;
            // The roster goes before the next event. Another worker may send
            // an event meanwhile: the roster deletes only the GUIDs that are
            // not on it, so the order does not matter for the memory.
            sendRoster = m_rosterState == RosterState::Due || m_rosterState == RosterState::Retry;
            if (sendRoster)
            {
                m_rosterState = RosterState::Sending;
                roster = m_roster;
            }
            else
            {
                request = std::move(m_requests.front());
                m_requests.pop_front();
                m_inFlight.push_back(request.event.groupId);
            }
        }
        if (sendRoster)
        {
            SendRoster(*connection, roster);
            // Then one event, also when the roster had no answer: while the
            // service is down, the events still go (and fail) in turn.
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopping || m_requests.empty())
                continue;
            request = std::move(m_requests.front());
            m_requests.pop_front();
            m_inFlight.push_back(request.event.groupId);
        }
        Handle(*connection, request);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inFlight.erase(std::find(m_inFlight.begin(), m_inFlight.end(), request.event.groupId));
    }
}

void Bots::ChatClient::Handle(ChatConnection& connection, Request const& request)
{
    ChatPostResult const result = connection.Post(ChatEventsPath, EncodeChatEvent(request.event));
    std::int64_t const arrivedAt = m_clock();

    if (!result.answered)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_rosterState == RosterState::Sent)
                m_rosterState = RosterState::Retry;
        }
        Log("no answer to " + Describe(request.event) + ": " + result.error);
        return;
    }
    if (result.status != 200)
    {
        Log(DescribeChatServiceError(result.status, result.body) + ", to " + Describe(request.event));
        return;
    }
    ChatResponse response;
    std::string error;
    if (!DecodeChatResponse(result.body, response, error))
    {
        Log("a bad answer to " + Describe(request.event) + ": " + error);
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    for (ChatLine const& line : response.lines)
    {
        HeldLine held;
        held.due.line = line;
        held.due.groupId = request.event.groupId;
        held.due.playerGuid = request.event.player.guid;
        held.dueAt = arrivedAt + line.delayMs;
        held.earliestAt = request.postedAt + line.delayMs;
        held.order = m_nextOrder++;
        m_lines.push_back(held);
    }
}

void Bots::ChatClient::SendRoster(ChatConnection& connection, std::vector<Guid> const& botGuids)
{
    ChatPostResult const result = connection.Post(ChatRosterPath, EncodeChatRoster(botGuids));
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_rosterState = result.answered ? RosterState::Sent : RosterState::Retry;
    }
    std::string const what = "the roster of " + std::to_string(botGuids.size()) + " bots";
    if (!result.answered)
    {
        Log("no answer to " + what + ": " + result.error);
        return;
    }
    if (result.status != 200)
    {
        Log(DescribeChatServiceError(result.status, result.body) + ", to " + what);
        return;
    }
    std::vector<Guid> deleted;
    std::string error;
    if (!DecodeChatRosterAnswer(result.body, deleted, error))
    {
        Log("a bad answer to " + what + ": " + error);
        return;
    }
    std::string text = "the chat service has " + what + ", and deleted the memory of " +
        std::to_string(deleted.size()) + " other bots";
    for (std::size_t i = 0; i < deleted.size(); ++i)
        text += (i ? ", " : ": ") + std::to_string(deleted[i]);
    Log(text);
}

void Bots::ChatClient::Log(std::string const& text)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping)
            return;
    }
    if (m_log)
        m_log(text);
}
