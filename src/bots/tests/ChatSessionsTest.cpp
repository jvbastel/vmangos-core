#include "ChatSessions.h"

#include <gtest/gtest.h>

using namespace Bots;

namespace
{
    ChatEventBot const Grumbar{101, "Grumbar", "orc", "warrior", "tank"};
    ChatEventBot const Lirael{102, "Lirael", "troll", "priest", "healer"};

    ChatGroupView Group(std::vector<ChatEventBot> bots, bool playerOnline = true)
    {
        ChatGroupView view;
        view.groupId = 42;
        view.player = {7, "Jvb"};
        view.playerOnline = playerOnline;
        view.bots = std::move(bots);
        view.zone = "Durotar";
        return view;
    }

    std::vector<Guid> Guids(std::vector<ChatEventBot> const& bots)
    {
        std::vector<Guid> guids;
        for (ChatEventBot const& bot : bots)
            guids.push_back(bot.guid);
        return guids;
    }
}

TEST(ChatSessions, AGroupWithThePlayerAndBotsStartsASessionSilently)
{
    ChatSessions sessions;

    EXPECT_TRUE(sessions.Update({Group({Grumbar, Lirael})}).empty());
    EXPECT_TRUE(sessions.Update({Group({Grumbar, Lirael})}).empty());
}

TEST(ChatSessions, ABotThatLeavesEndsItsSession)
{
    ChatSessions sessions;
    sessions.Update({Group({Grumbar, Lirael})});

    std::vector<ChatEvent> const events = sessions.Update({Group({Lirael})});

    ASSERT_EQ(events.size(), 1u);
    ChatEvent const& end = events[0];
    EXPECT_EQ(end.kind, ChatEventKind::SessionEnd);
    EXPECT_EQ(end.reason, SessionEndReason::BotLeft);
    EXPECT_EQ(end.endedBotGuids, (std::vector<Guid>{101}));
    EXPECT_EQ(end.groupId, 42u);
    EXPECT_EQ(end.player.guid, 7u);
    EXPECT_EQ(end.player.name, "Jvb");
    EXPECT_EQ(end.zone, "Durotar");
    // The bots of the session, so that the event names at least one bot.
    EXPECT_EQ(Guids(end.bots), (std::vector<Guid>{101, 102}));

    // The other bot keeps its session.
    EXPECT_TRUE(sessions.Update({Group({Lirael})}).empty());
}

TEST(ChatSessions, TheLastBotThatLeavesEndsItsSession)
{
    ChatSessions sessions;
    sessions.Update({Group({Grumbar})});

    std::vector<ChatEvent> const events = sessions.Update({Group({})});

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].reason, SessionEndReason::BotLeft);
    EXPECT_EQ(events[0].endedBotGuids, (std::vector<Guid>{101}));
    EXPECT_TRUE(sessions.Update({Group({})}).empty());
}

TEST(ChatSessions, AGroupThatIsGoneEndsTheSessionsOfAllItsBots)
{
    ChatSessions sessions;
    sessions.Update({Group({Grumbar, Lirael})});

    std::vector<ChatEvent> const events = sessions.Update({});

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].reason, SessionEndReason::GroupDisbanded);
    EXPECT_EQ(events[0].endedBotGuids, (std::vector<Guid>{101, 102}));
    EXPECT_TRUE(sessions.Update({}).empty());
}

TEST(ChatSessions, APlayerLogoutEndsTheSessionsOfAllBots)
{
    ChatSessions sessions;
    sessions.Update({Group({Grumbar, Lirael})});

    std::vector<ChatEvent> const events = sessions.Update({Group({Grumbar, Lirael}, false)});

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].reason, SessionEndReason::PlayerLogout);
    EXPECT_EQ(events[0].endedBotGuids, (std::vector<Guid>{101, 102}));
    // While the player is away, nothing more.
    EXPECT_TRUE(sessions.Update({Group({Grumbar, Lirael}, false)}).empty());
    // The player comes back: a new session, and its end comes again.
    EXPECT_TRUE(sessions.Update({Group({Grumbar, Lirael})}).empty());
    EXPECT_EQ(sessions.Update({}).size(), 1u);
}

TEST(ChatSessions, AGroupWithNoPlayerHasNoSession)
{
    ChatSessions sessions;
    ChatGroupView botsOnly = Group({Grumbar, Lirael});
    botsOnly.player = {};

    EXPECT_TRUE(sessions.Update({botsOnly}).empty());
    EXPECT_TRUE(sessions.Update({}).empty());
}

TEST(ChatSessions, ThePlayerThatLeavesTheGroupEndsTheSessions)
{
    ChatSessions sessions;
    sessions.Update({Group({Grumbar, Lirael})});
    ChatGroupView botsOnly = Group({Grumbar, Lirael});
    botsOnly.player = {};

    std::vector<ChatEvent> const events = sessions.Update({botsOnly});

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].reason, SessionEndReason::GroupDisbanded);
    EXPECT_EQ(events[0].player.guid, 7u);
}

TEST(ChatSessions, EachGroupHasItsOwnSessions)
{
    ChatSessions sessions;
    ChatGroupView other = Group({Lirael});
    other.groupId = 43;
    other.player = {8, "Other"};
    sessions.Update({Group({Grumbar}), other});

    std::vector<ChatEvent> const events = sessions.Update({other});

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].groupId, 42u);
    EXPECT_EQ(sessions.Groups(), (std::vector<std::uint32_t>{43}));
}

TEST(ChatSessions, AChatEventCopiesTheFactsOfTheGroup)
{
    ChatEvent const event = MakeChatEvent(Group({Grumbar, Lirael}), ChatEventKind::PlayerMessage);

    EXPECT_EQ(event.kind, ChatEventKind::PlayerMessage);
    EXPECT_EQ(event.groupId, 42u);
    EXPECT_EQ(event.player.guid, 7u);
    EXPECT_EQ(Guids(event.bots), (std::vector<Guid>{101, 102}));
    EXPECT_EQ(event.zone, "Durotar");
}

namespace
{
    GroupEvent Event(GroupEventKind kind)
    {
        GroupEvent event = {kind};
        return event;
    }
}

TEST(GroupChatEvent, ABossKillNamesTheBoss)
{
    GroupEvent kill = Event(GroupEventKind::BossKill);
    kill.target = 0xF130002CFD000101;
    kill.entry = 11517;
    ChatEvent event;

    ASSERT_TRUE(MakeGroupChatEvent(Group({Grumbar, Lirael}), kill, "Oggleflint", event));

    EXPECT_EQ(event.kind, ChatEventKind::BossKill);
    EXPECT_EQ(event.groupId, 42u);
    EXPECT_EQ(event.player.name, "Jvb");
    EXPECT_EQ(Guids(event.bots), (std::vector<Guid>{101, 102}));
    EXPECT_EQ(event.zone, "Durotar");
    EXPECT_EQ(event.bossName, "Oggleflint");
}

TEST(GroupChatEvent, AWipeHasOnlyTheGroupFacts)
{
    ChatEvent event;

    ASSERT_TRUE(MakeGroupChatEvent(Group({Grumbar}), Event(GroupEventKind::Wipe), "", event));

    EXPECT_EQ(event.kind, ChatEventKind::Wipe);
    EXPECT_EQ(event.bossName, "");
}

TEST(GroupChatEvent, AWipeInABossFightNamesTheBoss)
{
    GroupEvent wipe = Event(GroupEventKind::Wipe);
    wipe.entry = 11519;
    ChatEvent event;

    ASSERT_TRUE(MakeGroupChatEvent(Group({Grumbar}), wipe, "Bazzalan", event));

    EXPECT_EQ(event.kind, ChatEventKind::Wipe);
    EXPECT_EQ(event.bossName, "Bazzalan");
}

TEST(GroupChatEvent, ALootRollNamesTheItemAndTheWinner)
{
    GroupEvent roll = Event(GroupEventKind::LootRoll);
    roll.entry = 2140;
    roll.winner = 7; // The player won.
    ChatEvent event;

    ASSERT_TRUE(MakeGroupChatEvent(Group({Grumbar}), roll, "Carving Knife", event));

    EXPECT_EQ(event.kind, ChatEventKind::LootRoll);
    EXPECT_EQ(event.itemName, "Carving Knife");
    EXPECT_EQ(event.winnerGuid, 7u);
}

TEST(GroupChatEvent, ARestNamesTheBotThatRests)
{
    GroupEvent rest = Event(GroupEventKind::Rest);
    rest.bot = 102;
    rest.reason = RestReason::Drink;
    ChatEvent event;

    ASSERT_TRUE(MakeGroupChatEvent(Group({Grumbar, Lirael}), rest, "", event));

    EXPECT_EQ(event.kind, ChatEventKind::Rest);
    EXPECT_EQ(event.restingBotGuid, 102u);
}

TEST(GroupChatEvent, NoEventWithoutAChatSession)
{
    ChatEvent event;

    EXPECT_FALSE(MakeGroupChatEvent(Group({Grumbar}, false), Event(GroupEventKind::Wipe), "", event));
    EXPECT_FALSE(MakeGroupChatEvent(Group({}), Event(GroupEventKind::Wipe), "", event));
    ChatGroupView noPlayer = Group({Grumbar});
    noPlayer.player = {};
    EXPECT_FALSE(MakeGroupChatEvent(noPlayer, Event(GroupEventKind::Wipe), "", event));
}

TEST(GroupChatEvent, NoRestEventForABotThatIsNotInTheGroup)
{
    GroupEvent rest = Event(GroupEventKind::Rest);
    rest.bot = 999;
    ChatEvent event;

    EXPECT_FALSE(MakeGroupChatEvent(Group({Grumbar}), rest, "", event));
}
