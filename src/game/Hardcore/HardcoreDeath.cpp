/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// The final death of a hardcore character (WOWAI#253): the death hook (status, lock, death record, no
// self-resurrect, the message, the logout 60 seconds later), the resurrect and release guards, the login
// refusal with the death record, and the row of a deleted character.

#include "Hardcore.h"
#include "Database/DatabaseEnv.h"
#include "Database/SQLStorages.h"
#include "Log.h"
#include "Map.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Unit.h"
#include "WorldSession.h"
#include "Packets/Character.h"

#include <ctime>
#include <memory>
#include <string>

namespace
{
    // The time between the death and the logout: the player looks around and chats (spec #250 story 17).
    uint32 const LogoutDelayMs = 60 * IN_MILLISECONDS;

    // The innermost DeathCause scope open on this thread (map threads kill at the same time).
    thread_local Hardcore::DeathCause const* openCauses = nullptr;

    char const* EnvironmentName(int32 type)
    {
        switch (type)
        {
            case DAMAGE_EXHAUSTED:    return "fatigue";
            case DAMAGE_DROWNING:     return "drowning";
            case DAMAGE_FALL:         return "a fall";
            case DAMAGE_LAVA:         return "lava";
            case DAMAGE_SLIME:        return "slime";
            case DAMAGE_FIRE:         return "fire";
            case DAMAGE_FALL_TO_VOID: return "a fall into the void";
            default:                  return "the environment";
        }
    }

    // The killer's name, or the cause. A killer other than the victim wins; else an environmental cause;
    // else the victim killed itself (a GM `.die` without kill credit deals the damage as the victim).
    std::string KillerOf(Player const* victim)
    {
        int32 environment = -1;
        for (Hardcore::DeathCause const* cause = openCauses; cause; cause = cause->Outer())
        {
            if (cause->Killer() && cause->Killer() != victim)
                return cause->Killer()->GetName();
            if (environment < 0 && cause->Environment() >= 0)
                environment = cause->Environment();
        }
        if (environment >= 0)
            return EnvironmentName(environment);
        return "its own hand";
    }

    std::string ZoneName(uint32 zone)
    {
        AreaEntry const* entry = sAreaStorage.LookupEntry<AreaEntry>(zone);
        return entry && entry->Name ? entry->Name : "an unknown place";
    }

    std::string DeathRecordLine(std::string const& name, uint64 when, uint32 zone, std::string const& killer, uint32 level)
    {
        char date[32] = "an unknown time";
        time_t const t = time_t(when);
        if (struct tm const* utc = gmtime(&t))
            strftime(date, sizeof(date), "%Y-%m-%d %H:%M UTC", utc);
        return name + " died for good on " + date + " in " + ZoneName(zone) + ", killed by " + killer
            + ", at level " + std::to_string(level) + ".";
    }
}

Hardcore::DeathCause::DeathCause(Unit const* killer) : m_outer(openCauses), m_killer(killer), m_environment(-1)
{
    openCauses = this;
}

Hardcore::DeathCause::DeathCause(uint32 environmentalDamageType)
    : m_outer(openCauses), m_killer(nullptr), m_environment(int32(environmentalDamageType))
{
    openCauses = this;
}

Hardcore::DeathCause::~DeathCause()
{
    openCauses = m_outer;
}

void Hardcore::OnDeath(Player* player)
{
    if (!player || GetStatus(player->GetGUIDLow()) != Status::Alive)
        return;

    uint32 const guid = player->GetGUIDLow();
    uint64 const when = uint64(time(nullptr));
    uint32 const zone = player->GetZoneId();
    uint32 const level = player->GetLevel();
    std::string const killer = KillerOf(player);

    // Database: direct (synchronous) writes, not the 15-minute save, so a server stop or a crash cannot
    // undo the death. First the status (the guards read it), then the lock flag that LoadFromDB refuses,
    // then the death record. The flag also goes into m_characterFlags, so the save at the logout keeps it.
    SetStatus(guid, Status::Dead);
    player->SetCharacterFlag(CHARACTER_FLAG_LOCKED_FOR_TRANSFER, true);
    CharacterDatabase.DirectPExecute("UPDATE `characters` SET `character_flags` = `character_flags` | %u WHERE `guid` = %u",
        uint32(CHARACTER_FLAG_LOCKED_FOR_TRANSFER), guid);
    std::string escaped = killer.substr(0, 64);
    CharacterDatabase.escape_string(escaped);
    CharacterDatabase.DirectPExecute("UPDATE `character_hardcore` SET `death_time` = " UI64FMTD ", `death_zone` = %u, "
        "`death_killer` = '%s', `death_level` = %u WHERE `guid` = %u", when, zone, escaped.c_str(), level, guid);

    // Client: no self-resurrect spell, so the release dialog shows no Soulstone or Reincarnation button
    // (Unit::Kill sends the field right after this hook).
#if SUPPORTED_CLIENT_BUILD >= CLIENT_BUILD_1_6_1
    player->SetUInt32Value(PLAYER_SELF_RES_SPELL, 0);
#else
    player->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_CAN_SELF_RESURRECT);
    player->SetResurrectionSpellId(0);
#endif

    // Client: the message at once, as a system line in the chat and on the screen.
    player->SendSysMessage("Your hardcore character is dead forever. You will be logged out in 60 seconds.");
    player->GetSession()->SendNotification("Your hardcore character is dead forever.");
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "Hardcore: %s (guid %u) died for good: killed by %s in zone %u at level %u",
        player->GetName(), guid, killer.c_str(), zone, level);

    // The logout 60 seconds later, as a logout that the player asked for: WorldSession::Update logs the
    // character out (it saves it) and sends SMSG_LOGOUT_COMPLETE, so the client goes back to the character
    // list. The event dies with the player object, if the character leaves the world sooner. A bot has no
    // client and does not log out this way (WOWAI#257).
    player->m_Events.AddLambdaEventAtOffset([player]()
    {
        // ShouldLogOut is true 20 seconds after the request time: the next session update logs out.
        player->GetSession()->LogoutRequest(time(nullptr) - 20);
    }, LogoutDelayMs);
}

bool Hardcore::AllowResurrect(Player* player)
{
    if (!player || GetStatus(player->GetGUIDLow()) != Status::Dead)
        return true;
    // Database and client: nothing changes; the character stays dead. The player is told why.
    player->SendSysMessage("A hardcore character's death is final. Nothing can bring it back.");
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "Hardcore: refused the resurrect of %s (guid %u)", player->GetName(), player->GetGUIDLow());
    return false;
}

bool Hardcore::AllowRelease(Player* player)
{
    if (!IsHardcore(player))
        return true;
    // Database and client: nothing changes; the character stays where it died, with no ghost.
    player->SendSysMessage("A hardcore character cannot release its spirit. It stays where it died.");
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "Hardcore: refused the release of %s (guid %u)", player->GetName(), player->GetGUIDLow());
    return false;
}

bool Hardcore::AllowLogin(WorldSession* session, ObjectGuid const& guid)
{
    uint32 const low = guid.GetCounter();
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
        "SELECT c.`name`, h.`death_time`, h.`death_zone`, h.`death_killer`, h.`death_level` "
        "FROM `character_hardcore` h JOIN `characters` c ON c.`guid` = h.`guid` "
        "WHERE h.`guid` = %u AND c.`account` = %u AND h.`status` = %u",
        low, session->GetAccountId(), uint32(Status::Dead)));
    if (!result)
        return true;

    Field* fields = result->Fetch();
    std::string const record = DeathRecordLine(fields[0].GetCppString(), fields[1].GetUInt64(), fields[2].GetUInt32(),
        fields[3].GetCppString(), fields[4].GetUInt32());

    // Client: the 1.12 error "character locked" on the character list, then the death record as a
    // notification. Database: nothing changes. This runs before the core loads the character or takes the
    // one still in the world (a relog in the 60 seconds after the death).
    auto failed = std::make_unique<WorldPackets::Character::CharacterLoginFailed>();
    failed->result = CHAR_LOGIN_LOCKED_FOR_TRANSFER;
    session->SendPacket(std::move(failed));
    session->SendNotification("%s", record.c_str());
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "Hardcore: refused the login of a dead hardcore character: %s", record.c_str());
    return false;
}

void Hardcore::OnDelete(uint32 guid)
{
    // Database: in the transaction of Player::DeleteFromDB. The cache may keep the guid's old status: a new
    // character never gets the guid of a deleted one while the server runs, and the cache starts empty.
    CharacterDatabase.PExecute("DELETE FROM `character_hardcore` WHERE `guid` = %u", guid);
}
