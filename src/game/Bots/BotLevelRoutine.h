/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/


#ifndef WOWAI_GAME_BOTLEVELROUTINE_H
#define WOWAI_GAME_BOTLEVELROUTINE_H

#include "Common.h"
#include "SpellFile.h"

#include <map>
#include <string>
#include <vector>

class Pet;
class Player;
struct TrainerSpellData;

// The level routine: it makes a bot correct for its level L. It is
// idempotent, so it is safe to run twice. It teaches
// - the class quest spells of level L or lower, from the spell file of the class,
// - every trainer spell rank of the class trainers up to level L, with the
//   logic of `.learn all_trainer`, but no profession, riding, or pet trainer,
// - the weapon skills of the class that the bot's weapons use, worn or in
//   its bags, and it raises every weapon skill to 5 times L,
// - the pet spells of level L or lower to the bot's pet.
// Talent picks come in a later ticket.
class BotLevelRoutine
{
    public:
        // Reads `<dataDir>/spells/<class>.txt` of each class. At startup.
        void Load(std::string const& dataDir);

        void Apply(Player* bot) const;
        // Also on each summon of the pet.
        void TeachPetSpells(Player* bot, Pet* pet) const;

    private:
        void TeachQuestSpells(Player* bot) const;
        void TeachTrainerSpells(Player* bot) const;
        void TeachWeaponSkills(Player* bot) const;
        std::vector<TrainerSpellData const*> const& ClassTrainerSpells(uint8 botClass) const;

        std::map<uint8 /*class*/, std::vector<Bots::ClassSpell>> m_classSpells;
        mutable std::map<uint8 /*class*/, std::vector<TrainerSpellData const*>> m_trainerSpells;
};

#endif
