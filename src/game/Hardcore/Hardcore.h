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

    // The status of the character with the low guid `guid`. Reads its row once, then the cache. Works for a
    // character that is offline.
    Status GetStatus(uint32 guid);

    // True for Alive and Dead: the character has the hardcore status.
    bool IsHardcore(uint32 guid);
    bool IsHardcore(Player const* player);

    // Writes the status of `guid` to character_hardcore at once (a new row, or the `status` column of its
    // row) and to the cache. For the module's own files: the NPC, the death (#253) and the drop (#255).
    void SetStatus(uint32 guid, Status status);

    // The hardcore NPC offers the status: the character is level 1, has 0 XP and was never hardcore.
    bool CanTakeStatus(Player const* player);

    // The player takes the status at the hardcore NPC. Written at once, with the time. False (and nothing
    // changes) when CanTakeStatus is false.
    bool TakeStatus(Player* player);

    // The answer of `.hardcore`, one line: "Hardcore: alive" for a hardcore character; for any other
    // character, that it is not hardcore and whether it dropped the status before.
    std::string StatusLine(Player const* player);

    // The hardcore NPC offers "drop the status": the character is a hardcore character that is alive. A dead
    // hardcore character keeps its final death.
    bool CanDropStatus(Player const* player);

    // The player drops the hardcore status at a hardcore NPC (WOWAI#255). One call drops it for the player
    // and for every hardcore member of the player's group that is alive (the hardcore bots too, online or
    // not): each becomes Dropped ("was hardcore", never again), written at once with the time. Each online
    // group member that dropped with the player is told in its chat; the NPC script tells the player.
    // Returns how many characters dropped the status: 0 (and nothing changes) when CanDropStatus is false.
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
    // is not hardcore, in both directions.
    bool AllowGroupInvite(Player* inviter, Player const* invited);

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

    // Hook: WorldSession::HandlePlayerLoginOpcode, before the core loads the character or takes the one
    // still in the world. False (with the 1.12 error "character locked" and the death record) for a dead
    // hardcore character of the session's account. Reads the database, so it holds after a restart.
    bool AllowLogin(WorldSession* session, ObjectGuid const& guid);

    // Hook: Player::DeleteFromDB, in its transaction of a full delete: the character's character_hardcore
    // row goes with it.
    void OnDelete(uint32 guid);

    // --- Hardcore bots (WOWAI#257, HardcoreBots.cpp) ---
    // A hardcore bot is a hardcore character: its death is OnDeath's, and DropStatus drops it with the
    // player. These calls come from the bot game layer (src/game/Bots), not from stock code.

    // The bot manager made the bot `guid` with the hardcore option: its status is Alive, written at once
    // with the time. False when the write failed.
    bool MakeBotHardcore(uint32 guid);

    // The bot manager adds the bot `botGuid` (online or not) to the group of `player`: false across the
    // hardcore border, as AllowGroupInvite (the bot manager's join does not pass the invite handler), and
    // for a dead hardcore bot. The player is told the reason.
    bool AllowBotInGroup(Player* player, uint32 botGuid);

    // The bot manager logs the bot `guid` in: false (with a line for the asker in `error`) for a dead
    // hardcore bot. Never again, also after a server restart.
    bool AllowBotLogin(uint32 guid, std::string& error);

    // OnDeath's logout 60 seconds after the death, for a bot (it has no client to log out): the bot leaves
    // its group and logs out at the next update of the bot manager. False for a character that is no bot.
    bool LogOutDeadBot(Player* player);
}

#endif
