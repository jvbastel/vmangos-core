#include "ChatContract.h"

#include <gtest/gtest.h>

using namespace Bots;

namespace
{
    // The example group of the chat service tests (tests/events.py).
    ChatEvent Event(ChatEventKind kind)
    {
        ChatEvent event;
        event.kind = kind;
        event.groupId = 42;
        event.player = {7, "Jvb"};
        event.bots = {
            {101, "Grumbar", "orc", "warrior", "tank"},
            {102, "Lirael", "troll", "priest", "healer"},
        };
        event.zone = "Ragefire Chasm";
        return event;
    }

    std::string const Facts =
        R"("group_id":42,"player":{"guid":7,"name":"Jvb"},)"
        R"("bots":[{"guid":101,"name":"Grumbar","race":"orc","class":"warrior","role":"tank"},)"
        R"({"guid":102,"name":"Lirael","race":"troll","class":"priest","role":"healer"}],)"
        R"("zone":"Ragefire Chasm")";
}

TEST(ChatContract, TheContractVersionIsOne)
{
    EXPECT_EQ(ChatContractVersion, 1);
}

TEST(ChatContract, APartyMessageHasNoWhisperedBot)
{
    ChatEvent event = Event(ChatEventKind::PlayerMessage);
    event.message = {ChatChannel::Party, "nice one", 0};

    EXPECT_EQ(EncodeChatEvent(event),
        R"({"contract_version":1,"event":"player_message",)" + Facts +
        R"(,"message":{"channel":"party","text":"nice one"}})");
}

TEST(ChatContract, AWhisperNamesTheWhisperedBot)
{
    ChatEvent event = Event(ChatEventKind::PlayerMessage);
    event.message = {ChatChannel::Whisper, "you ok?", 102};

    EXPECT_EQ(EncodeChatEvent(event),
        R"({"contract_version":1,"event":"player_message",)" + Facts +
        R"(,"message":{"channel":"whisper","text":"you ok?","to_bot_guid":102}})");
}

TEST(ChatContract, EachEventCarriesItsOwnFacts)
{
    ChatEvent boss = Event(ChatEventKind::BossKill);
    boss.bossName = "Taragaman the Hungerer";
    EXPECT_EQ(EncodeChatEvent(boss),
        R"({"contract_version":1,"event":"boss_kill",)" + Facts + R"(,"boss_name":"Taragaman the Hungerer"})");

    ChatEvent wipe = Event(ChatEventKind::Wipe);
    EXPECT_EQ(EncodeChatEvent(wipe), R"({"contract_version":1,"event":"wipe",)" + Facts + "}");
    wipe.bossName = "Bazzalan";
    EXPECT_EQ(EncodeChatEvent(wipe), R"({"contract_version":1,"event":"wipe",)" + Facts + R"(,"boss_name":"Bazzalan"})");

    ChatEvent loot = Event(ChatEventKind::LootRoll);
    loot.itemName = "Cavedweller Bracers";
    loot.winnerGuid = 7;
    EXPECT_EQ(EncodeChatEvent(loot),
        R"({"contract_version":1,"event":"loot_roll",)" + Facts + R"(,"item_name":"Cavedweller Bracers","winner_guid":7})");

    ChatEvent rest = Event(ChatEventKind::Rest);
    rest.restingBotGuid = 102;
    EXPECT_EQ(EncodeChatEvent(rest), R"({"contract_version":1,"event":"rest",)" + Facts + R"(,"bot_guid":102})");

    ChatEvent end = Event(ChatEventKind::SessionEnd);
    end.endedBotGuids = {101, 102};
    end.reason = SessionEndReason::GroupDisbanded;
    EXPECT_EQ(EncodeChatEvent(end),
        R"({"contract_version":1,"event":"session_end",)" + Facts + R"(,"ended_bot_guids":[101,102],"reason":"group_disbanded"})");
    end.reason = SessionEndReason::BotLeft;
    EXPECT_NE(EncodeChatEvent(end).find(R"("reason":"bot_left")"), std::string::npos);
    end.reason = SessionEndReason::PlayerLogout;
    EXPECT_NE(EncodeChatEvent(end).find(R"("reason":"player_logout")"), std::string::npos);
}

TEST(ChatContract, ThePlayerTextIsEscaped)
{
    ChatEvent event = Event(ChatEventKind::PlayerMessage);
    event.message = {ChatChannel::Party, "say \"hi\"\\ now\n", 0};

    EXPECT_NE(EncodeChatEvent(event).find(R"("text":"say \"hi\"\\ now\n")"), std::string::npos);
}

TEST(ChatContract, TextThatIsNoValidUtf8DoesNotStopTheEvent)
{
    ChatEvent event = Event(ChatEventKind::PlayerMessage);
    event.message = {ChatChannel::Party, std::string("ok \xff"), 0};

    EXPECT_NE(EncodeChatEvent(event).find(R"("text":"ok )"), std::string::npos);
}

TEST(ChatContract, AResponseGivesTheLinesInSpeakingOrder)
{
    ChatResponse response;
    std::string error;

    ASSERT_TRUE(DecodeChatResponse(
        R"({"contract_version":1,"lines":[)"
        R"({"bot_guid":102,"channel":"whisper","text":"im fine","delay_ms":900},)"
        R"({"bot_guid":101,"channel":"party","text":"lol","delay_ms":1500}]})",
        response, error)) << error;

    EXPECT_EQ(response.contractVersion, 1);
    ASSERT_EQ(response.lines.size(), 2u);
    EXPECT_EQ(response.lines[0].botGuid, 102u);
    EXPECT_EQ(response.lines[0].channel, ChatChannel::Whisper);
    EXPECT_EQ(response.lines[0].text, "im fine");
    EXPECT_EQ(response.lines[0].delayMs, 900u);
    EXPECT_EQ(response.lines[1].botGuid, 101u);
    EXPECT_EQ(response.lines[1].channel, ChatChannel::Party);
    EXPECT_EQ(response.lines[1].delayMs, 1500u);
}

TEST(ChatContract, AResponseWithNoLinesIsValid)
{
    ChatResponse response;
    std::string error;

    EXPECT_TRUE(DecodeChatResponse(R"({"contract_version":1,"lines":[]})", response, error));
    EXPECT_TRUE(response.lines.empty());
}

TEST(ChatContract, ABrokenResponseIsAnError)
{
    ChatResponse response;
    std::string error;

    EXPECT_FALSE(DecodeChatResponse("not json", response, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(DecodeChatResponse(R"({"contract_version":1})", response, error));
    EXPECT_FALSE(DecodeChatResponse(
        R"({"contract_version":1,"lines":[{"bot_guid":101,"channel":"yell","text":"x","delay_ms":1}]})", response, error));
    EXPECT_FALSE(DecodeChatResponse(
        R"({"contract_version":1,"lines":[{"bot_guid":101,"channel":"party","text":"x","delay_ms":-5}]})", response, error));
    EXPECT_FALSE(DecodeChatResponse(
        R"({"contract_version":1,"lines":[{"bot_guid":101,"channel":"party","text":"","delay_ms":1}]})", response, error));
}

TEST(ChatContract, AResponseOfAnotherContractVersionIsAnError)
{
    ChatResponse response;
    std::string error;

    EXPECT_FALSE(DecodeChatResponse(R"({"contract_version":2,"lines":[]})", response, error));
    EXPECT_NE(error.find("contract version 2"), std::string::npos) << error;
}

TEST(ChatContract, TheServiceErrorNamesTheAcceptedVersions)
{
    EXPECT_EQ(DescribeChatServiceError(400, R"({"error":"unsupported_contract_version","accepted":[0,1]})"),
        "the chat service rejects contract version 1 (it accepts 0, 1)");
    EXPECT_EQ(DescribeChatServiceError(422, R"({"error":"invalid_event"})"), "the chat service answers HTTP 422: invalid_event");
    EXPECT_EQ(DescribeChatServiceError(500, "Internal Server Error"), "the chat service answers HTTP 500");
}
