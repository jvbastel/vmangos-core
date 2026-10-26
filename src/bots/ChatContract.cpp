#include "ChatContract.h"

#include <nlohmann/json.hpp>

#include <sstream>

using Json = nlohmann::json;

namespace
{
    // A JSON string. Text that is no valid UTF-8 gets replacement characters,
    // so that a bad byte in a player message does not stop the event.
    std::string Quote(std::string const& text)
    {
        return Json(text).dump(-1, ' ', false, Json::error_handler_t::replace);
    }

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

    char const* ChannelName(Bots::ChatChannel channel)
    {
        return channel == Bots::ChatChannel::Whisper ? "whisper" : "party";
    }

    char const* ReasonName(Bots::SessionEndReason reason)
    {
        switch (reason)
        {
            case Bots::SessionEndReason::BotLeft: return "bot_left";
            case Bots::SessionEndReason::GroupDisbanded: return "group_disbanded";
            case Bots::SessionEndReason::PlayerLogout: break;
        }
        return "player_logout";
    }

    // The type checks of the response schema for one line.
    bool DecodeLine(Json const& item, Bots::ChatLine& line, std::string& error)
    {
        if (!item.is_object())
        {
            error = "a line is no object";
            return false;
        }
        auto const guid = item.find("bot_guid");
        auto const channel = item.find("channel");
        auto const text = item.find("text");
        auto const delay = item.find("delay_ms");
        if (guid == item.end() || !guid->is_number_unsigned() || guid->get<Bots::Guid>() == 0)
        {
            error = "a line has no valid bot_guid";
            return false;
        }
        if (channel == item.end() || !channel->is_string() || (*channel != "party" && *channel != "whisper"))
        {
            error = "a line has no valid channel";
            return false;
        }
        if (text == item.end() || !text->is_string() || text->get<std::string>().empty())
        {
            error = "a line has no text";
            return false;
        }
        if (delay == item.end() || !delay->is_number_unsigned())
        {
            error = "a line has no valid delay_ms";
            return false;
        }
        line.botGuid = guid->get<Bots::Guid>();
        line.channel = *channel == "whisper" ? Bots::ChatChannel::Whisper : Bots::ChatChannel::Party;
        line.text = text->get<std::string>();
        line.delayMs = delay->get<std::uint32_t>();
        return true;
    }
}

std::string Bots::EncodeChatEvent(ChatEvent const& event)
{
    // By hand, in the order of the schema: nlohmann::json sorts the keys.
    std::ostringstream out;
    out << "{\"contract_version\":" << ChatContractVersion
        << ",\"event\":\"" << EventName(event.kind) << '"'
        << ",\"group_id\":" << event.groupId
        << ",\"player\":{\"guid\":" << event.player.guid << ",\"name\":" << Quote(event.player.name) << '}'
        << ",\"bots\":[";
    for (std::size_t i = 0; i < event.bots.size(); ++i)
    {
        ChatEventBot const& bot = event.bots[i];
        out << (i ? "," : "") << "{\"guid\":" << bot.guid << ",\"name\":" << Quote(bot.name)
            << ",\"race\":" << Quote(bot.race) << ",\"class\":" << Quote(bot.botClass)
            << ",\"role\":" << Quote(bot.role) << '}';
    }
    out << "],\"zone\":" << Quote(event.zone);

    switch (event.kind)
    {
        case ChatEventKind::PlayerMessage:
            out << ",\"message\":{\"channel\":\"" << ChannelName(event.message.channel)
                << "\",\"text\":" << Quote(event.message.text);
            if (event.message.channel == ChatChannel::Whisper)
                out << ",\"to_bot_guid\":" << event.message.toBotGuid;
            out << '}';
            break;
        case ChatEventKind::BossKill:
        case ChatEventKind::Wipe:
            if (!event.bossName.empty())
                out << ",\"boss_name\":" << Quote(event.bossName);
            break;
        case ChatEventKind::LootRoll:
            out << ",\"item_name\":" << Quote(event.itemName) << ",\"winner_guid\":" << event.winnerGuid;
            break;
        case ChatEventKind::Rest:
            out << ",\"bot_guid\":" << event.restingBotGuid;
            break;
        case ChatEventKind::SessionEnd:
            out << ",\"ended_bot_guids\":[";
            for (std::size_t i = 0; i < event.endedBotGuids.size(); ++i)
                out << (i ? "," : "") << event.endedBotGuids[i];
            out << "],\"reason\":\"" << ReasonName(event.reason) << '"';
            break;
    }
    out << '}';
    return out.str();
}

std::string Bots::EncodeChatRoster(std::vector<Guid> const& botGuids)
{
    std::ostringstream out;
    out << "{\"contract_version\":" << ChatContractVersion << ",\"bot_guids\":[";
    for (std::size_t i = 0; i < botGuids.size(); ++i)
        out << (i ? "," : "") << botGuids[i];
    out << "]}";
    return out.str();
}

bool Bots::DecodeChatRosterAnswer(std::string const& body, std::vector<Guid>& deleted, std::string& error)
{
    Json const json = Json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object())
    {
        error = "the answer is no JSON object";
        return false;
    }
    auto const version = json.find("contract_version");
    if (version == json.end() || !version->is_number_integer() || version->get<int>() != ChatContractVersion)
    {
        error = "the answer has no contract_version " + std::to_string(ChatContractVersion);
        return false;
    }
    auto const guids = json.find("deleted_bot_guids");
    if (guids == json.end() || !guids->is_array())
    {
        error = "the answer has no deleted_bot_guids";
        return false;
    }
    std::vector<Guid> decoded;
    for (Json const& guid : *guids)
    {
        if (!guid.is_number_unsigned())
        {
            error = "the answer has a GUID that is no number";
            return false;
        }
        decoded.push_back(guid.get<Guid>());
    }
    deleted = decoded;
    return true;
}

bool Bots::DecodeChatResponse(std::string const& body, ChatResponse& response, std::string& error)
{
    Json const json = Json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object())
    {
        error = "the answer is no JSON object";
        return false;
    }
    auto const version = json.find("contract_version");
    if (version == json.end() || !version->is_number_integer())
    {
        error = "the answer has no contract_version";
        return false;
    }
    if (version->get<int>() != ChatContractVersion)
    {
        error = "the answer has contract version " + std::to_string(version->get<int>()) +
            ", the core has " + std::to_string(ChatContractVersion);
        return false;
    }
    auto const lines = json.find("lines");
    if (lines == json.end() || !lines->is_array())
    {
        error = "the answer has no lines";
        return false;
    }

    ChatResponse decoded;
    decoded.contractVersion = version->get<int>();
    for (Json const& item : *lines)
    {
        ChatLine line;
        if (!DecodeLine(item, line, error))
            return false;
        decoded.lines.push_back(line);
    }
    response = decoded;
    return true;
}

std::string Bots::DescribeChatServiceError(int status, std::string const& body)
{
    Json const json = Json::parse(body, nullptr, false);
    std::string code;
    if (json.is_object() && json.count("error") && json["error"].is_string())
        code = json["error"].get<std::string>();

    if (code == "unsupported_contract_version")
    {
        std::string accepted;
        if (json.count("accepted") && json["accepted"].is_array())
            for (Json const& version : json["accepted"])
                if (version.is_number_integer())
                    accepted += (accepted.empty() ? "" : ", ") + std::to_string(version.get<int>());
        return "the chat service rejects contract version " + std::to_string(ChatContractVersion) +
            " (it accepts " + accepted + ")";
    }
    std::string text = "the chat service answers HTTP " + std::to_string(status);
    if (!code.empty())
        text += ": " + code;
    return text;
}
