#include "ChatClient.h"

#include <httplib/httplib.h>
#include <gtest/gtest.h>

#include <chrono>
#include <mutex>
#include <thread>

using namespace Bots;

namespace
{
    // A chat service on a free local port.
    struct TestService
    {
        httplib::Server server;
        std::thread thread;
        int port = 0;

        std::mutex mutex;
        std::vector<std::string> paths;
        std::vector<std::string> contentTypes;
        std::vector<std::string> bodies;
        std::vector<int> clientPorts;
        int status = 200;
        std::string answer = R"({"contract_version":1,"lines":[]})";
        int sleepMs = 0;

        TestService()
        {
            server.Post(".*", [this](httplib::Request const& request, httplib::Response& response) {
                int sleep;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    paths.push_back(request.path);
                    contentTypes.push_back(request.get_header_value("Content-Type"));
                    bodies.push_back(request.body);
                    clientPorts.push_back(request.remote_port);
                    response.status = status;
                    response.set_content(answer, "application/json");
                    sleep = sleepMs;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep));
            });
            port = server.bind_to_any_port("127.0.0.1");
            thread = std::thread([this]() { server.listen_after_bind(); });
            server.wait_until_ready();
        }

        ~TestService()
        {
            server.stop();
            thread.join();
        }

        ChatClientSettings Settings(unsigned timeoutMs = 2000) const
        {
            ChatClientSettings settings;
            settings.url = "http://127.0.0.1:" + std::to_string(port);
            settings.timeoutMs = timeoutMs;
            return settings;
        }
    };
}

TEST(ChatHttp, PostsTheEventAsJsonToV1Events)
{
    TestService service;
    service.answer = R"({"contract_version":1,"lines":[{"bot_guid":5,"channel":"party","text":"hi","delay_ms":0}]})";
    std::unique_ptr<ChatConnection> connection = NewHttpChatConnection(service.Settings());

    ChatPostResult const result = connection->Post(R"({"event":"rest"})");

    EXPECT_TRUE(result.answered) << result.error;
    EXPECT_EQ(result.status, 200);
    EXPECT_EQ(result.body, service.answer);
    ASSERT_EQ(service.paths.size(), 1u);
    EXPECT_EQ(service.paths[0], "/v1/events");
    EXPECT_EQ(service.contentTypes[0], "application/json");
    EXPECT_EQ(service.bodies[0], R"({"event":"rest"})");
}

TEST(ChatHttp, KeepsTheConnectionAliveBetweenEvents)
{
    TestService service;
    std::unique_ptr<ChatConnection> connection = NewHttpChatConnection(service.Settings());

    connection->Post("{}");
    connection->Post("{}");
    connection->Post("{}");

    ASSERT_EQ(service.clientPorts.size(), 3u);
    EXPECT_EQ(service.clientPorts[1], service.clientPorts[0]);
    EXPECT_EQ(service.clientPorts[2], service.clientPorts[0]);
}

TEST(ChatHttp, AnErrorStatusComesBackWithItsBody)
{
    TestService service;
    service.status = 400;
    service.answer = R"({"error":"unsupported_contract_version","accepted":[0,1]})";
    std::unique_ptr<ChatConnection> connection = NewHttpChatConnection(service.Settings());

    ChatPostResult const result = connection->Post("{}");

    EXPECT_TRUE(result.answered);
    EXPECT_EQ(result.status, 400);
    EXPECT_EQ(result.body, service.answer);
}

TEST(ChatHttp, ASlowServiceTimesOut)
{
    TestService service;
    service.sleepMs = 1500;
    std::unique_ptr<ChatConnection> connection = NewHttpChatConnection(service.Settings(200));

    auto const start = std::chrono::steady_clock::now();
    ChatPostResult const result = connection->Post("{}");
    auto const took = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result.answered);
    EXPECT_NE(result.error.find("no answer within 200 ms"), std::string::npos) << result.error;
    EXPECT_LT(took, std::chrono::milliseconds(1000));
}

TEST(ChatHttp, AServiceThatIsDownGivesNoAnswer)
{
    int port;
    {
        TestService gone;
        port = gone.port;
    }
    ChatClientSettings settings;
    settings.url = "http://127.0.0.1:" + std::to_string(port);
    settings.timeoutMs = 500;

    ChatPostResult const result = NewHttpChatConnection(settings)->Post("{}");

    EXPECT_FALSE(result.answered);
    EXPECT_FALSE(result.error.empty());
}

TEST(ChatHttp, TheClientGetsTheLinesOverHttp)
{
    TestService service;
    service.answer = R"({"contract_version":1,"lines":[{"bot_guid":101,"channel":"whisper","text":"sure","delay_ms":0}]})";
    ChatClient client(service.Settings(), []() { return std::int64_t(0); }, nullptr);

    ChatEvent event;
    event.groupId = 1;
    event.player = {7, "Jvb"};
    event.bots = {{101, "Grumbar", "orc", "warrior", "tank"}};
    event.message = {ChatChannel::Whisper, "hi", 101};
    client.Post(event);

    std::vector<DueChatLine> due;
    for (int i = 0; i < 200 && due.empty(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        due = client.TakeDue(0);
    }
    ASSERT_EQ(due.size(), 1u);
    EXPECT_EQ(due[0].line.text, "sure");
    EXPECT_EQ(due[0].line.channel, ChatChannel::Whisper);
}
