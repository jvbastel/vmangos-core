#include "ChatClient.h"

#include <httplib/httplib.h>

#include <chrono>

namespace
{
    // One kept-alive HTTP connection to the chat service. cpp-httplib opens
    // it at the first call and again after the service closed it.
    class HttpChatConnection : public Bots::ChatConnection
    {
    public:
        explicit HttpChatConnection(Bots::ChatClientSettings const& settings)
            : m_client(BaseUrl(settings.url)), m_timeout(settings.timeoutMs)
        {
            std::chrono::milliseconds const timeout = m_timeout;
            m_client.set_keep_alive(true);
            m_client.set_connection_timeout(timeout);
            m_client.set_read_timeout(timeout);
            m_client.set_write_timeout(timeout);
            // The whole call, not only one read.
            m_client.set_max_timeout(timeout);
        }

        Bots::ChatPostResult Post(std::string const& body) override
        {
            Bots::ChatPostResult result;
            auto const start = std::chrono::steady_clock::now();
            httplib::Result const answer = m_client.Post("/v1/events", body, "application/json");
            if (!answer)
            {
                result.error = httplib::to_string(answer.error());
                // A timeout shows as a failed read: say what it was.
                if (std::chrono::steady_clock::now() - start >= m_timeout)
                    result.error = "no answer within " + std::to_string(m_timeout.count()) + " ms (" + result.error + ")";
                return result;
            }
            result.answered = true;
            result.status = answer->status;
            result.body = answer->body;
            return result;
        }

    private:
        static std::string BaseUrl(std::string url)
        {
            while (!url.empty() && url.back() == '/')
                url.pop_back();
            return url;
        }

        httplib::Client m_client;
        std::chrono::milliseconds m_timeout;
    };
}

std::unique_ptr<Bots::ChatConnection> Bots::NewHttpChatConnection(ChatClientSettings const& settings)
{
    return std::unique_ptr<ChatConnection>(new HttpChatConnection(settings));
}
