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

    // Hook: WorldSession::HandlePlayerLogin, after the character is in the world. Reads the character's row
    // again, and shows "Hardcore: alive" in the chat of a hardcore character (the 1.12 client has no
    // hardcore icon).
    void OnLogin(Player* player);

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
    // self-resurrect spell back. For a hardcore character that is alive: the status Dead, the lock flag
    // CHARACTER_FLAG_LOCKED_FOR_TRANSFER and the death record (time, zone, killer or cause, level), all
    // written to the database at once; no self-resurrect spell (no Soulstone or Reincarnation button); the
    // message that the character is dead forever; and the logout 60 seconds later. Any other character:
    // nothing.
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
}

#endif
