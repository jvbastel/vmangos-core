#ifndef WOWAI_BOTS_INBOX_H
#define WOWAI_BOTS_INBOX_H

#include <mutex>
#include <utility>
#include <vector>

// The inbox of a bot. Other threads (the packet senders, later the chat
// workers) only push into it. The bot drains it in its update on the map
// thread, where all its decisions happen.
namespace Bots
{
    template <typename T>
    class Inbox
    {
    public:
        void Push(T item)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_items.push_back(std::move(item));
        }

        // All items pushed since the last drain, oldest first.
        std::vector<T> Drain()
        {
            std::vector<T> items;
            std::lock_guard<std::mutex> lock(m_mutex);
            items.swap(m_items);
            return items;
        }

    private:
        std::mutex m_mutex;
        std::vector<T> m_items;
    };
}

#endif
