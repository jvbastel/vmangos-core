/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_HARDCORE_H
#define WOWAI_GAME_HARDCORE_H

#include "Common.h"

#include <string>

class Group;
class ObjectGuid;
class Player;
class Unit;
class WorldSession;

// Hardcore characters on the play realm (spec jvbastel/WOWAI#250, ADR 0003). A hardcore character is a
// character with the hardcore status: its first death is final, and it gets no help from non-hardcore
// characters. The player takes the status at the hardcore NPC (script npc_hardcore, in
// src/scripts/custom/npc_hardcore.cpp) at level 1 with 0 XP.
//
// This header is the one facade of the hardcore module for the stock code and the NPC script. Each hook in
// an upstream file is one line: `Hardcore::On<Event>(...)` or `if (!Hardcore::Allow<Action>(...)) return;`,
// with its reason line in wowai/core-allowlist.txt ("Hardcore hook N: ...").
//
// The state lives in the characters table character_hardcore (one row per character that ever took the
// status). Each change is written to the database at once (a direct query), not with the 15-minute save,
// so a server stop or a crash after it cannot undo it. The module keeps the rows that it has read in a
// cache; a character's row is read again at each of its logins, so a row changed by SQL (a check) is seen
// at the next login.
namespace Hardcore
{
    // The `status` column of character_hardcore. A character with no row is Never.
    enum class Status : uint8
    {
        Never   = 0, // never took the status (no row)
        Alive   = 1, // a hardcore character that is alive
        Dead    = 2, // a hardcore character after its final death (WOWAI#253)
        Dropped = 3, // dropped the status at a hardcore NPC: "was hardcore", never again (WOWAI#255)
    };

    // The line of `.hardcore` and of the login for a hardcore character that is alive (the 1.12 client has no
    // hardcore icon). The NPC says it too when the player takes the status.
    constexpr char AliveLine[] = "Hardcore: alive";

    // The status of the character with the low guid `guid`. Reads its row once, then the cache. Works for a
    // character that is offline.
    Status GetStatus(uint32 guid);
    // The same for a character in the world; Never for null.
    Status GetStatus(Player const* player);

    // True for Alive and Dead: the character has the hardcore status.
    bool IsHardcore(uint32 guid);
    bool IsHardcore(Player const* player);

    // --- For the module's own files only: the two writes of the status (HardcoreStatus.cpp) ---

    // A new Alive row for `guid`, with the time, written at once; then the cache. The player at the NPC
    // (TakeStatus) and a hardcore bot at its creation (MakeBotHardcore). False (the cache forgets the guid,
    // so the next ask reads the row again) when the write failed, also when the row exists already.
    bool InsertAlive(uint32 guid);

    // Changes the status of `guid` from `from` to `to` in one direct UPDATE that changes only a row with the
    // status `from`, so two map threads cannot undo each other's change (a death and a drop at the same
    // moment). `columns` adds more `, col = value` assignments to the same UPDATE (the death record, the
    // drop time). Then it reads the row back: true when the status is `to` now. False (and the cache forgets
    // the guid) when the write failed or the row had another status.
    bool ChangeStatus(uint32 guid, Status from, Status to, std::string const& columns = std::string());

    // The hardcore NPC offers the status: the character is level 1, has 0 XP and was never hardcore.
    bool CanTakeStatus(Player const* player);

    // Why the NPC refuses the status to a character that CanTakeStatus offers it, or null: the character
    // is in a group with a character that is not hardcore (the group would cross the hardcore border).
    char const* TakeRefusal(Player const* player);

    // The player takes the status at the hardcore NPC. Written at once, with the time. False (and nothing
    // changes) when CanTakeStatus is false or TakeRefusal names a reason.
    bool TakeStatus(Player* player);

    // The answer of `.hardcore`, one line: "Hardcore: alive" for a hardcore character; for any other
    // character, that it is not hardcore and whether it dropped the status before.
    std::string StatusLine(Player const* player);

    // The hardcore NPC offers "drop the status": the character is a hardcore character that is alive. A dead
    // hardcore character keeps its final death.
    bool CanDropStatus(Player const* player);

    // Why the NPC refuses the drop to a character that CanDropStatus offers it, or null: another player
    // (not a bot) in the group is a hardcore character that is alive. The drop takes the group's hardcore
    // bots along, never another player's status without that player's own word.
    char const* DropRefusal(Player const* player);

    // The player drops the hardcore status at a hardcore NPC (WOWAI#255). One call drops it for the player
    // and for every hardcore bot of the player's group that is alive (online or not): each becomes Dropped
    // ("was hardcore", never again), written at once with the time. Hardcore bots out of the group keep
    // their status (the NPC's warning says so). Each online bot that dropped with the player is told in its
    // chat; the NPC script tells the player. Returns how many characters dropped the status: 0 (and nothing
    // changes) when CanDropStatus is false or DropRefusal names a reason.
    uint32 DropStatus(Player* player);

    // Hook: WorldSession::HandlePlayerLogin, after the character is in the world. Reads the character's row
    // again, and shows "Hardcore: alive" in the chat of a hardcore character (the 1.12 client has no
    // hardcore icon).
    void OnLogin(Player* player);

    // No help across the hardcore border (WOWAI#254, src/game/Hardcore/HardcoreRules.cpp). Each Allow* is the
    // check of one stock handler: false refuses the action. The module then tells the acting character the
    // hardcore reason (a system line, and the refusal packet that its window waits for); the hook only
    // returns. Two hardcore characters, and two characters that are not hardcore, are not limited. Guilds are
    // not limited (a 1.12 guild moves no items or gold).

    // Hook: WorldSession::HandleInitiateTradeOpcode. Refused between a hardcore character and a character
    // that is not hardcore, in both directions.
    bool AllowTrade(Player* player, Player const* other);

    // Hook: WorldSession::HandleSendMail, with the receiver's low guid (it may be offline). Refused from a
    // character that is not hardcore to a hardcore character (text, money, item and cash on delivery), and
    // cash on delivery from a hardcore character to one that is not (its payment is gold across the border).
    // Mail sent by the game (MailDraft: quests, auctions, GM commands) does not pass here, so it arrives.
    bool AllowMail(Player* sender, uint32 receiverGuid, bool cashOnDelivery);

    // Hooks: WorldSession::HandleAuctionHelloOpcode, and WorldSession::GetCheckedAuctionHouseForAuctioneer (so
    // a client that skips the hello cannot sell, bid, cancel or list). Refused for a hardcore character.
    bool AllowAuctionHouse(Player* player);

    // Hook: WorldSession::HandleGroupInviteOpcode. Refused between a hardcore character and a character that
    // is not hardcore, in both directions; for a dead hardcore character (inviter or invited); and for a
    // hardcore bot above the level of the hardcore character that invites it.
    bool AllowGroupInvite(Player* inviter, Player const* invited);

    // Hook: Group::AddMember, the one place where every path puts a member into a group (the invite accept,
    // the bot manager, a scripted run, the stock party bots `.partybot`, the meeting stone). Refused when the
    // new member `guid` (online or not) would be in a group with a member across the hardcore border, or is
    // a dead hardcore character. A battleground's own raid group is not limited: the battleground puts every
    // fighter of a side into it, and a refusal would leave the side without its raid. The new member and the
    // leader, when online, are told the reason; the caller's stock failure path runs (a refused stock party
    // bot has no group and leaves the world at its next update).
    bool AllowGroupMember(Group const* group, ObjectGuid const& guid);

    // The reason that the character `joiner` cannot join a group with the character `member` in it (low
    // guids, online or not), or null: the joiner is a dead hardcore character, or exactly one of the two is
    // hardcore. The one group rule of AllowGroupInvite, AllowGroupMember and AllowBotInGroup.
    char const* GroupRefusal(uint32 joiner, uint32 member);

    // --- The final death (WOWAI#253, HardcoreDeath.cpp) ---

    // What killed a character, for its death record. A scope object on the stack of the code that kills:
    // Unit::Kill (the killer) and Player::EnvironmentalDamage (a fall, drowning, fatigue, lava, slime or
    // fire). OnDeath reads the scopes that are open on its thread when the death comes. One line each in
    // the stock code; it changes nothing in the game.
    class DeathCause
    {
    public:
        explicit DeathCause(Unit const* killer);              // Unit::Kill: `this`
        explicit DeathCause(uint32 environmentalDamageType);  // Player::EnvironmentalDamage: its `type`
        ~DeathCause();
        DeathCause(DeathCause const&) = delete;
        DeathCause& operator=(DeathCause const&) = delete;

        DeathCause const* Outer() const { return m_outer; }
        Unit const* Killer() const { return m_killer; }
        int32 Environment() const { return m_environment; }   // -1: none

    private:
        DeathCause const* m_outer;
        Unit const* m_killer;
        int32 m_environment;
    };

    // Hook: Player::SetDeathState, at the "just died" state of a living player, after the core put the
    // self-resurrect spell back; also at the corpse state of a living player (the logout of a priest in
    // Spirit of Redemption kills it without the "just died" state). For a hardcore character that is
    // alive: the status Dead, the lock flag CHARACTER_FLAG_LOCKED_FOR_TRANSFER and the death record (time,
    // zone, killer or cause, level), all written to the database at once; no self-resurrect spell (no
    // Soulstone or Reincarnation button); the message that the character is dead forever; and the logout 60
    // seconds later. Any other character: nothing.
    void OnDeath(Player* player);

    // Hook: Player::ResurrectPlayer. False for a dead hardcore character: no resurrect by any path (spells,
    // Soulstone, Reincarnation, the spirit healer, GM commands, scripted runs).
    bool AllowResurrect(Player* player);

    // Hook: WorldSession::HandleRepopRequestOpcode. False for a hardcore character: no release, no ghost.
    bool AllowRelease(Player* player);

    // Hooks: Player::BuildPlayerRepop, Player::ScheduleRepopAtGraveyard and Player::RepopAtGraveyard, the
    // paths to a ghost or to the graveyard that no packet asks for: the 6-minute auto release, the fall under
    // the map, a battleground (insignia, spirit guide, the start-area check), the Spirit of Redemption
    // logout and the stock bots' release. False for a dead hardcore character: the body stays where it died.
    // A living character (a fall under the map that it survives) still goes to the graveyard. Silent: some
    // of these paths ask at every update.
    bool AllowRepop(Player const* player);

    // Hook: WorldSession::HandlePlayerLoginOpcode, before the core loads the character or takes the one
    // still in the world. False (with the 1.12 error "character locked" and the death record) for a dead
    // hardcore character of the session's account. Reads the database, so it holds after a restart.
    bool AllowLogin(WorldSession* session, ObjectGuid const& guid);

    // Hook: Player::DeleteFromDB, in its transaction of a full delete: the character's character_hardcore
    // row goes with it.
    void OnDelete(uint32 guid);

    // Hook: WorldSession::LogoutPlayer. A dead hardcore character leaves its group (and its original group,
    // in a battleground) at its logout: the 60-second logout after the death, or a logout that came sooner.
    // Its seat is free, and its bots, with no player left, leave the group and log out (BotMgr). Any other
    // character: nothing.
    void OnLogout(Player* player);

    // --- No escape by a lost connection (WOWAI#256, HardcoreDisconnect.cpp) ---

    // Hook: WorldSession::ForcePlayerLogoutDelay, when the client of a player (not a bot) is gone. True for a
    // hardcore character that is alive and in combat: the stock delayed logout keeps it in the world (2
    // minutes), even with the realm setting ForceLogoutDelay off, and a death in that time is final. False
    // for any other character: the realm setting decides, as before.
    bool KeepInWorldAfterLostConnection(Player const* player);

    // --- Hardcore bots (WOWAI#257, HardcoreBots.cpp) ---
    // A hardcore bot is a hardcore character: its death is OnDeath's, and DropStatus drops it with the
    // player. These calls come from the bot game layer (src/game/Bots), not from stock code.

    // The bot manager made the bot `guid` with the hardcore option: its status is Alive, written at once
    // with the time. False when the write failed.
    bool MakeBotHardcore(uint32 guid);

    // True for a character of the WOWAI bots (the bot_character table), online or not.
    bool IsBot(uint32 guid);

    // Why the hardcore bot `botGuid` (online or not) cannot join the group of the hardcore character
    // `player`, or null: the bot is above the player's level (spec #250 story 39: a hardcore bot is at or
    // below the player's level, also after the player made it). Null for any other pair.
    char const* BotLevelRefusal(Player const* player, uint32 botGuid);

    // The bot manager adds the bot `botGuid` (online or not) to the group of `player`: false for GroupRefusal
    // and BotLevelRefusal, before the bot logs in for the join (Group::AddMember checks the group rule again).
    // The player is told the reason.
    bool AllowBotInGroup(Player* player, uint32 botGuid);

    // The bot manager logs the bot `guid` in: false (with a line for the asker in `error`) for a dead
    // hardcore bot. Never again, also after a server restart.
    bool AllowBotLogin(uint32 guid, std::string& error);

    // OnDeath's logout 60 seconds after the death, for a bot (it has no client to log out): the bot leaves
    // its group and logs out at the next update of the bot manager. False for a character that is no bot.
    bool LogOutDeadBot(Player* player);
}

#endif
