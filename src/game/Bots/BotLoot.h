/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_BOTLOOT_H
#define WOWAI_GAME_BOTLOOT_H

#include "Common.h"
#include "Loot.h"
#include "ObjectGuid.h"

#include <map>
#include <set>
#include <utility>

class Player;
class Creature;
class WorldPacket;
struct ItemPrototype;

// A loot roll that a bot saw end: SMSG_LOOT_ROLL_WON. The bot chat event
// `loot_roll` (#50) comes from here.
struct BotLootRollWon
{
    ObjectGuid lootedTarget;
    uint32 itemSlot = 0;
    uint32 itemEntry = 0;
    ObjectGuid winner;
    uint8 rollNumber = 0;
    uint8 rollType = 0;   // ROLL_NEED or ROLL_GREED: the kind of the winning roll.
    bool botRolled = false; // The bot rolled on the item. Then `botRoll` is its roll.
    Bots::LootRoll botRoll = Bots::LootRoll::Greed;
};

// The loot of one bot: need or greed, corpse loot, the bag-full rule, and
// putting on the clear upgrades that it gets. The decisions are in the
// decision library (Loot.h). See "8. Loot, death, and rest: Loot" in the
// version 1 build spec.
//
// Everything runs in BotAI::UpdateAI on the map thread. The bot acts through
// the client packets of its socketless session (loot, store, money, release,
// roll, and equip), so the core checks each step like for a player.
class BotLoot
{
    public:
        // SMSG_LOOT_START_ROLL: the bot rolls need or greed.
        void OnStartRoll(Player* bot, WorldPacket const& packet);
        // SMSG_LOOT_ROLL_WON. A bot that won an item with full bags goes back
        // to the corpse for it.
        BotLootRollWon OnRollWon(Player* bot, WorldPacket const& packet);

        // At each think of a living bot out of combat. `mayMove`: the command
        // state lets the bot walk to a corpse. True while the bot is busy
        // with a corpse; the bot then does not follow.
        bool Think(Player* bot, bool mayMove);

        // Stops the corpse work, for example when a fight starts.
        void Stop();

    private:
        enum class Step
        {
            None,      // No corpse.
            Walk,      // The bot walks to the corpse.
            Open,      // The bot asked to open the corpse.
            Release,   // The bot took the loot and closes the corpse.
        };

        Creature* FindCorpse(Player* bot) const;
        // Asks for every item that the bot may take, and the money. False
        // when an item is left for lack of room that the bot can make in
        // this pass.
        bool TakeLoot(Player* bot, Creature* corpse);
        // The bag-full rule, for an item that does not fit. True when the
        // bot deleted a grey item for it.
        bool MakeRoom(Player* bot, ItemPrototype const* proto, uint32 count);
        void PutOnUpgrades(Player* bot);
        void Finish(Player* bot);

        Step m_step = Step::None;
        ObjectGuid m_corpse;
        uint32 m_stepThinks = 0; // Thinks in the current step, against a stuck bot.
        // The bot took all that it could from the corpse, so it does not
        // open the corpse again. Only an unfinished bag-full rule leaves it false.
        bool m_lootedAll = true;
        // Corpses that the bot has looted, so that it does not open them again.
        std::set<ObjectGuid> m_looted;
        // Bag items that the bot tried to put on.
        std::set<ObjectGuid> m_triedToEquip;
        // The rolls of the bot that are not over yet: (corpse, loot slot).
        std::map<std::pair<ObjectGuid, uint32>, Bots::LootRoll> m_rolls;
};

#endif
