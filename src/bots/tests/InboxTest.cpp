#include "Inbox.h"

#include <gtest/gtest.h>

#include <thread>

using Bots::Inbox;

TEST(Inbox, DrainGivesThePushedItemsInOrderOnce)
{
    Inbox<int> inbox;
    inbox.Push(1);
    inbox.Push(2);
    inbox.Push(3);

    EXPECT_EQ(inbox.Drain(), (std::vector<int>{1, 2, 3}));
    EXPECT_TRUE(inbox.Drain().empty());
}

TEST(Inbox, KeepsEveryItemThatOtherThreadsPush)
{
    Inbox<int> inbox;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&inbox]() {
            for (int i = 0; i < 1000; ++i)
                inbox.Push(i);
        });

    std::size_t drained = 0;
    for (int round = 0; round < 100; ++round)
        drained += inbox.Drain().size();
    for (std::thread& thread : threads)
        thread.join();
    drained += inbox.Drain().size();

    EXPECT_EQ(drained, 4000u);
}
