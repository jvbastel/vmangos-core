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
#include "TalentBuild.h"

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
// - the pet spells of level L or lower to the bot's pet,
// - the first L minus 9 picks of the bot's talent build, with
//   Player::LearnTalent. A bot with a talent rank that those picks do not
//   have (more picks than L minus 9, or a talent off the build) gets
//   ResetTalents(true) first.
class BotLevelRoutine
{
    public:
        // Reads `<dataDir>/spells/<class>.txt` of each class, and the talent
        // builds `<dataDir>/talents/*.txt`. At startup, after the DBC stores.
        void Load(std::string const& dataDir);

        // `talentBuild` is the name of the bot's talent build, for example
        // "warrior_tank". With no build of that name, the bot spends no picks.
        void Apply(Player* bot, std::string const& talentBuild) const;
        // Also on each summon of the pet. It then turns on the autocast of
        // the pet spells of the class module (Bots::PetAutocastSpells).
        void TeachPetSpells(Player* bot, Pet* pet) const;

    private:
        void TurnOnPetAutocast(Player* bot, Pet* pet) const;
        void TeachQuestSpells(Player* bot) const;
        void TeachTrainerSpells(Player* bot) const;
        void TeachWeaponSkills(Player* bot) const;
        void SpendTalentPicks(Player* bot, std::string const& talentBuild) const;
        void LoadTalentBuilds(std::string const& talentDir);
        std::vector<TrainerSpellData const*> const& ClassTrainerSpells(uint8 botClass) const;

        std::map<uint8 /*class*/, std::vector<Bots::ClassSpell>> m_classSpells;
        std::map<std::string /*name*/, Bots::TalentBuild> m_talentBuilds;
        mutable std::map<uint8 /*class*/, std::vector<TrainerSpellData const*>> m_trainerSpells;
};

#endif
