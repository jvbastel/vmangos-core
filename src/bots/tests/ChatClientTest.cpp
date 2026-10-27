#include "ChatClient.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace Bots;

namespace
{
    // A chat service in memory. Each test sets the answer. With `hold`, a
    // request waits until the test calls Release().
    struct FakeService
    {
        std::mutex mutex;
        std::condition_variable changed;
        std::vector<std::string> bodies;
        std::vector<std::string> paths;
        ChatPostResult answer;
        ChatPostResult rosterAnswer = OkRoster();
        bool hold = false;
        bool holdRoster = false;
        int inFlight = 0;
        int connections = 0;
        // The clock jumps to this time when a request ends (a slow service).
        std::int64_t answerAt = -1;
        std::atomic<std::int64_t>* clock = nullptr;

        static ChatPostResult OkRoster()
        {
            ChatPostResult result;
            result.answered = true;
            result.status = 200;
            result.body = R"({"contract_version":1,"deleted_bot_guids":[]})";
            return result;
        }

        ChatPostResult Post(std::string const& path, std::string const& body)
        {
            std::unique_lock<std::mutex> lock(mutex);
            bodies.push_back(body);
            paths.push_back(path);
            if (path == ChatRosterPath)
            {
                changed.notify_all();
                changed.wait(lock, [this]() { return !holdRoster; });
                return rosterAnswer;
            }
            ++inFlight;
            changed.notify_all();
            changed.wait(lock, [this]() { return !hold; });
            --inFlight;
            if (answerAt >= 0)
                *clock = answerAt;
            return answer;
        }

        void Release()
        {
            std::lock_guard<std::mutex> lock(mutex);
            hold = false;
            changed.notify_all();
        }

        // Waits (at most 5 s) until the service got `count` requests.
        bool WaitForRequests(std::size_t count)
        {
            std::unique_lock<std::mutex> lock(mutex);
            return changed.wait_for(lock, std::chrono::seconds(5), [&]() { return bodies.size() >= count; });
        }

        bool WaitInFlight(int count)
        {
            std::unique_lock<std::mutex> lock(mutex);
            return changed.wait_for(lock, std::chrono::seconds(5), [&]() { return inFlight >= count; });
        }
    };

    class FakeConnection : public ChatConnection
    {
    public:
        explicit FakeConnection(FakeService& service) : m_service(service) {}
        ChatPostResult Post(std::string const& path, std::string const& body) override { return m_service.Post(path, body); }

    private:
        FakeService& m_service;
    };

    ChatPostResult Answer(std::string const& body, int status = 200)
    {
        ChatPostResult result;
        result.answered = true;
        result.status = status;
        result.body = body;
        return result;
    }

    std::string const TwoLines =
        R"({"contract_version":1,"lines":[)"
        R"({"bot_guid":101,"channel":"party","text":"hi","delay_ms":1000},)"
        R"({"bot_guid":102,"channel":"party","text":"yo","delay_ms":2500}]})";

    ChatEvent PartyMessage(std::string const& text = "hello")
    {
        ChatEvent event;
        event.kind = ChatEventKind::PlayerMessage;
        event.groupId = 42;
        event.player = {7, "Jvb"};
        event.bots = {{101, "Grumbar", "orc", "warrior", "tank"}, {102, "Lirael", "troll", "priest", "healer"}};
        event.zone = "Durotar";
        event.message = {ChatChannel::Party, text, 0};
        return event;
    }

    struct ClientTest : ::testing::Test
    {
        FakeService service;
        std::atomic<std::int64_t> now{0};
        std::mutex logMutex;
        std::vector<std::string> log;
        std::unique_ptr<ChatClient> client;

        void Start(unsigned workers = 1, std::size_t queueSize = 8)
        {
            service.clock = &now;
            ChatClientSettings settings;
            settings.workers = workers;
            settings.queueSize = queueSize;
            client.reset(new ChatClient(settings,
                [this]() { return now.load(); },
                [this](std::string const& line) {
                    std::lock_guard<std::mutex> lock(logMutex);
                    log.push_back(line);
                },
                [this]() {
                    std::lock_guard<std::mutex> lock(service.mutex);
                    ++service.connections;
                    return std::unique_ptr<ChatConnection>(new FakeConnection(service));
                }));
        }

        // Waits until the client has no request left.
        void WaitIdle()
        {
            for (int i = 0; i < 500 && client->Pending() > 0; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ASSERT_EQ(client->Pending(), 0u);
        }

        // The paths that the service got, in order.
        std::vector<std::string> Paths()
        {
            std::lock_guard<std::mutex> lock(service.mutex);
            return service.paths;
        }

        std::vector<std::string> Log()
        {
            std::lock_guard<std::mutex> lock(logMutex);
            return log;
        }
    };
}

TEST_F(ClientTest, PostSendsTheEventAsJson)
{
    service.answer = Answer(R"({"contract_version":1,"lines":[]})");
    Start();

    client->Post(PartyMessage("hello there"));

    ASSERT_TRUE(service.WaitForRequests(1));
    EXPECT_EQ(service.bodies[0], EncodeChatEvent(PartyMessage("hello there")));
}

TEST_F(ClientTest, ALineIsDueAfterItsDelayFromTheAnswer)
{
    service.answer = Answer(TwoLines);
    service.answerAt = 5000;
    Start();

    client->Post(PartyMessage());
    WaitIdle();

    EXPECT_TRUE(client->TakeDue(5999).empty());
    std::vector<DueChatLine> due = client->TakeDue(6000);
    ASSERT_EQ(due.size(), 1u);
    EXPECT_EQ(due[0].line.botGuid, 101u);
    EXPECT_EQ(due[0].line.text, "hi");
    EXPECT_EQ(due[0].line.channel, ChatChannel::Party);
    EXPECT_EQ(due[0].groupId, 42u);
    EXPECT_EQ(due[0].playerGuid, 7u);
    EXPECT_TRUE(client->TakeDue(6000).empty());

    due = client->TakeDue(7500);
    ASSERT_EQ(due.size(), 1u);
    EXPECT_EQ(due[0].line.botGuid, 102u);
}

TEST_F(ClientTest, LinesThatAreDueTogetherComeInSpeakingOrder)
{
    service.answer = Answer(TwoLines);
    Start();

    client->Post(PartyMessage());
    WaitIdle();

    std::vector<DueChatLine> due = client->TakeDue(3000);
    ASSERT_EQ(due.size(), 2u);
    EXPECT_EQ(due[0].line.text, "hi");
    EXPECT_EQ(due[1].line.text, "yo");
}

TEST_F(ClientTest, ALineMoreThanTenSecondsLateDrops)
{
    // The player spoke at 0 and the service answered at 9500: the first line
    // could have come at 1000 and is 9.5 s late at 10500.
    service.answer = Answer(TwoLines);
    service.answerAt = 9500;
    Start();

    client->Post(PartyMessage());
    WaitIdle();

    std::vector<DueChatLine> due = client->TakeDue(10500);
    ASSERT_EQ(due.size(), 1u);
    EXPECT_EQ(due[0].line.text, "hi");
    // The second line could have come at 2500: at 12501 it is 10.001 s late.
    EXPECT_TRUE(client->TakeDue(12501).empty());
    EXPECT_TRUE(client->TakeDue(100000).empty());
}

TEST_F(ClientTest, AFullQueueDropsTheOldestRequest)
{
    service.answer = Answer(R"({"contract_version":1,"lines":[]})");
    service.hold = true;
    Start(1, 2);

    client->Post(PartyMessage("first"));
    ASSERT_TRUE(service.WaitForRequests(1));
    client->Post(PartyMessage("second"));
    client->Post(PartyMessage("third"));
    client->Post(PartyMessage("fourth"));
    service.Release();
    WaitIdle();

    ASSERT_EQ(service.bodies.size(), 3u);
    EXPECT_EQ(service.bodies[1], EncodeChatEvent(PartyMessage("third")));
    EXPECT_EQ(service.bodies[2], EncodeChatEvent(PartyMessage("fourth")));
    std::vector<std::string> const lines = Log();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NE(lines[0].find("queue is full"), std::string::npos) << lines[0];
}

TEST_F(ClientTest, AFixedPoolOfWorkersSendsAtTheSameTime)
{
    service.answer = Answer(R"({"contract_version":1,"lines":[]})");
    service.hold = true;
    Start(3, 8);

    for (int i = 0; i < 5; ++i)
        client->Post(PartyMessage());

    ASSERT_TRUE(service.WaitInFlight(3));
    EXPECT_EQ(client->Pending(), 5u);
    service.Release();
    WaitIdle();
    EXPECT_EQ(service.bodies.size(), 5u);
    // One kept connection for each worker.
    EXPECT_EQ(service.connections, 3);
}

TEST_F(ClientTest, AServiceThatIsDownGivesOneLogLineAndNoLine)
{
    service.answer.answered = false;
    service.answer.error = "Could not establish connection";
    Start();

    client->Post(PartyMessage());
    WaitIdle();

    EXPECT_TRUE(client->TakeDue(100000).empty());
    std::vector<std::string> const lines = Log();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NE(lines[0].find("player_message"), std::string::npos) << lines[0];
    EXPECT_NE(lines[0].find("Could not establish connection"), std::string::npos) << lines[0];
}

TEST_F(ClientTest, ARejectedContractVersionGivesOneLogLineAndNoLine)
{
    service.answer = Answer(R"({"error":"unsupported_contract_version","accepted":[2,3]})", 400);
    Start();

    client->Post(PartyMessage());
    WaitIdle();

    EXPECT_TRUE(client->TakeDue(100000).empty());
    std::vector<std::string> const lines = Log();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NE(lines[0].find("rejects contract version 1 (it accepts 2, 3)"), std::string::npos) << lines[0];
}

TEST_F(ClientTest, ABrokenAnswerGivesOneLogLineAndNoLine)
{
    service.answer = Answer("<html>bad gateway</html>");
    Start();

    client->Post(PartyMessage());
    WaitIdle();

    EXPECT_TRUE(client->TakeDue(100000).empty());
    EXPECT_EQ(Log().size(), 1u);
}

TEST_F(ClientTest, AGroupHasChatPendingUntilItsLastLineIsTaken)
{
    // The end of a scripted run waits for this (jvbastel/WOWAI#82, spec
    // section 10): the line of the last boss_kill must still find the bots
    // in the group.
    service.answer = Answer(TwoLines);
    service.hold = true;
    Start();
    ChatEvent other = PartyMessage("other group");
    other.groupId = 43;

    EXPECT_FALSE(client->HasPendingChat(42));
    client->Post(PartyMessage());
    EXPECT_TRUE(client->HasPendingChat(42)); // The request waits or is sent.
    ASSERT_TRUE(service.WaitInFlight(1));
    EXPECT_TRUE(client->HasPendingChat(42));
    EXPECT_FALSE(client->HasPendingChat(43));
    service.Release();
    WaitIdle();

    EXPECT_TRUE(client->HasPendingChat(42)); // Two lines are held.
    EXPECT_EQ(client->TakeDue(1000).size(), 1u);
    EXPECT_TRUE(client->HasPendingChat(42)); // One line is held.
    EXPECT_EQ(client->TakeDue(2500).size(), 1u);
    EXPECT_FALSE(client->HasPendingChat(42));

    client->Post(other);
    WaitIdle();
    EXPECT_FALSE(client->HasPendingChat(42));
    EXPECT_TRUE(client->HasPendingChat(43));
    // A line that drops (too late) ends the wait too.
    EXPECT_TRUE(client->TakeDue(100000).empty());
    EXPECT_FALSE(client->HasPendingChat(43));
}

TEST_F(ClientTest, TheClientStopsWhileARequestWaits)
{
    service.answer = Answer(R"({"contract_version":1,"lines":[]})");
    service.hold = true;
    Start();
    client->Post(PartyMessage());
    client->Post(PartyMessage());
    ASSERT_TRUE(service.WaitForRequests(1));

    std::thread release([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        service.Release();
    });
    client.reset();
    release.join();

    // The queued request is not sent after the stop.
    EXPECT_EQ(service.bodies.size(), 1u);
}

// A scripted run ties each line to its event (spec section 10: each required
// event reaches the chat service, and a line arrives in game chat).
TEST_F(ClientTest, EachPostHasAnIdThatItsLinesCarry)
{
    service.answer = Answer(TwoLines);
    Start();

    std::uint64_t const first = client->Post(PartyMessage());
    std::uint64_t const second = client->Post(PartyMessage());
    WaitIdle();

    EXPECT_NE(first, 0u);
    EXPECT_NE(second, first);
    std::vector<DueChatLine> const due = client->TakeDue(3000);
    ASSERT_EQ(due.size(), 4u);
    EXPECT_EQ(due[0].eventId, first);
    EXPECT_EQ(due[0].eventKind, ChatEventKind::PlayerMessage);
    // The first lines of both answers come before the second lines.
    EXPECT_EQ(due[1].eventId, second);
    EXPECT_EQ(due[2].eventId, first);
}

TEST_F(ClientTest, TellsWhichEventsTheServiceAnswered)
{
    service.answer = Answer(TwoLines);
    Start();
    std::uint64_t const answered = client->Post(PartyMessage());
    WaitIdle();
    service.answer.answered = false;
    std::uint64_t const lost = client->Post(PartyMessage());
    WaitIdle();

    std::vector<ChatEventOutcome> const outcomes = client->TakeOutcomes();
    ASSERT_EQ(outcomes.size(), 2u);
    EXPECT_EQ(outcomes[0].eventId, answered);
    EXPECT_TRUE(outcomes[0].reached);
    EXPECT_EQ(outcomes[1].eventId, lost);
    EXPECT_FALSE(outcomes[1].reached);
    EXPECT_TRUE(client->TakeOutcomes().empty());
}

TEST_F(ClientTest, ADroppedRequestDidNotReachTheService)
{
    service.answer = Answer(R"({"contract_version":1,"lines":[]})");
    service.hold = true;
    Start(1, 1);

    client->Post(PartyMessage("first"));
    ASSERT_TRUE(service.WaitForRequests(1));
    std::uint64_t const dropped = client->Post(PartyMessage("second"));
    client->Post(PartyMessage("third"));
    service.Release();
    WaitIdle();

    std::vector<ChatEventOutcome> const outcomes = client->TakeOutcomes();
    auto const found = std::find_if(outcomes.begin(), outcomes.end(),
        [dropped](ChatEventOutcome const& outcome) { return outcome.eventId == dropped; });
    ASSERT_NE(found, outcomes.end());
    EXPECT_FALSE(found->reached);
}

TEST_F(ClientTest, TheRosterOfTheBotGuidsGoesFirst)
{
    service.answer = Answer(R"({"contract_version":1,"lines":[]})");
    Start();

    client->SetRoster({101, 102});
    client->Post(PartyMessage());

    ASSERT_TRUE(service.WaitForRequests(2));
    EXPECT_EQ(service.paths[0], ChatRosterPath);
    EXPECT_EQ(service.bodies[0], EncodeChatRoster({101, 102}));
    EXPECT_EQ(service.paths[1], ChatEventsPath);
}

TEST_F(ClientTest, TheRosterGoesOnceWhileTheServiceAnswers)
{
    service.answer = Answer(R"({"contract_version":1,"lines":[]})");
    Start(2);

    client->SetRoster({101});
    ASSERT_TRUE(service.WaitForRequests(1));
    client->SetRoster({101, 103}); // a new bot
    client->Post(PartyMessage());
    client->Post(PartyMessage());
    WaitIdle();

    std::vector<std::string> const paths = Paths();
    EXPECT_EQ(std::count(paths.begin(), paths.end(), ChatRosterPath), 1);
    EXPECT_EQ(paths.size(), 3u);
}

TEST_F(ClientTest, AfterTheServiceWasDownTheNextCallSendsTheNewestRosterAgain)
{
    service.answer = Answer(R"({"contract_version":1,"lines":[]})");
    service.rosterAnswer.answered = false;
    service.rosterAnswer.error = "Connection";
    Start();

    client->SetRoster({101});
    ASSERT_TRUE(service.WaitForRequests(1));
    // No loop while the service is down: the roster waits for the next call.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(Paths().size(), 1u);

    service.rosterAnswer = FakeService::OkRoster();
    client->SetRoster({101, 103});
    client->Post(PartyMessage());
    WaitIdle();

    std::vector<std::string> const paths = Paths();
    ASSERT_EQ(paths.size(), 3u);
    EXPECT_EQ(paths[1], ChatRosterPath);
    EXPECT_EQ(service.bodies[1], EncodeChatRoster({101, 103}));
    EXPECT_EQ(paths[2], ChatEventsPath);
}

TEST_F(ClientTest, AnEventWithNoAnswerMakesTheRosterGoAgain)
{
    service.answer.answered = false;
    service.answer.error = "Connection";
    Start();
    client->SetRoster({101});
    client->Post(PartyMessage()); // the service went down after the roster
    WaitIdle();
    ASSERT_EQ(Paths().size(), 2u);

    service.answer = Answer(R"({"contract_version":1,"lines":[]})");
    client->Post(PartyMessage());
    WaitIdle();

    std::vector<std::string> const paths = Paths();
    ASSERT_EQ(paths.size(), 4u);
    EXPECT_EQ(paths[2], ChatRosterPath);
    EXPECT_EQ(paths[3], ChatEventsPath);
}

TEST_F(ClientTest, AServiceWithNoRosterRouteGivesOneLogLineAndNoRetry)
{
    service.answer = Answer(R"({"contract_version":1,"lines":[]})");
    service.rosterAnswer = Answer(R"({"detail":"Not Found"})", 404);
    Start();

    client->SetRoster({101});
    client->Post(PartyMessage());
    client->Post(PartyMessage());
    WaitIdle();

    std::vector<std::string> const paths = Paths();
    EXPECT_EQ(std::count(paths.begin(), paths.end(), ChatRosterPath), 1);
    std::vector<std::string> const log = Log();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_NE(log[0].find("roster"), std::string::npos) << log[0];
}

TEST_F(ClientTest, WhileTheServiceIsDownTheEventsStillGoInTurn)
{
    service.answer.answered = false;
    service.answer.error = "Connection";
    service.rosterAnswer.answered = false;
    service.rosterAnswer.error = "Connection";
    Start();

    client->SetRoster({101});
    client->Post(PartyMessage());
    client->Post(PartyMessage());
    WaitIdle();

    std::vector<std::string> const paths = Paths();
    EXPECT_EQ(std::count(paths.begin(), paths.end(), ChatEventsPath), 2);
}

TEST_F(ClientTest, NoEventGoesWhileTheRosterIsSent)
{
    // An event of a bot made after the roster was taken must not reach the
    // service before the roster: the roster would delete the new rows.
    service.answer = Answer(R"({"contract_version":1,"lines":[]})");
    service.holdRoster = true;
    Start(2);

    client->SetRoster({101});
    ASSERT_TRUE(service.WaitForRequests(1));
    client->SetRoster({101, 103}); // a new bot
    client->Post(PartyMessage());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(Paths().size(), 1u);

    {
        std::lock_guard<std::mutex> lock(service.mutex);
        service.holdRoster = false;
    }
    service.changed.notify_all();
    WaitIdle();

    std::vector<std::string> const paths = Paths();
    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(paths[1], ChatEventsPath);
}
