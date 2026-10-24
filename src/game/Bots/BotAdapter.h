/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_BOTADAPTER_H
#define WOWAI_GAME_BOTADAPTER_H

#include "BotActions.h"
#include "BotView.h"

class Player;
class Unit;

// The real adapter of the decision library on one bot: BotView answers from
// the Player and the units of its map, and BotActions acts as the Player.
// Units are GUIDs in the library; the adapter finds them on the map of the
// bot, so a unit on another map is a unit that the bot cannot see.
//
// Threads: only on the map thread of the bot, in BotAI::UpdateAI.
class BotAdapter : public Bots::BotView, public Bots::BotActions
{
    public:
        explicit BotAdapter(Player* me) : me(me) {}

        // BotView
        Bots::Guid GetSelf() const override;
        Bots::Health GetHealth(Bots::Guid unit) const override;
        Bots::SpellId GetCurrentCast(Bots::Guid unit) const override;
        Bots::Power GetPower(Bots::Guid unit) const override;
        float GetDistance(Bots::Guid unit) const override;
        bool IsInCombat(Bots::Guid unit) const override;
        Bots::Guid GetVictim(Bots::Guid unit) const override;
        std::vector<Bots::Guid> GetAttackers(Bots::Guid unit) const override;
        bool HasAura(Bots::Guid unit, Bots::SpellId firstRank) const override;
        Bots::Guid GetFollowTarget() const override;
        bool KnowsSpell(Bots::SpellId spell) const override;
        Bots::SpellId GetNextRank(Bots::SpellId spell) const override;
        bool CanCast(Bots::SpellId spell, Bots::Guid target) const override;
        std::vector<Bots::Guid> GetGroupMembers() const override;

        // BotActions
        void CastSpell(Bots::SpellId spell, Bots::Guid target) override;
        void MoveTo(Bots::Position const& destination) override;
        void Attack(Bots::Guid target) override;
        void Chase(Bots::Guid target) override;
        void Follow(Bots::Guid unit) override;

    private:
        // The unit on the map of the bot, in the world, or null.
        Unit* Find(Bots::Guid unit) const;

        Player* me;
};

#endif
