/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

#ifndef WOWAI_GAME_BOTGEAR_H
#define WOWAI_GAME_BOTGEAR_H

#include "Common.h"
#include "GearDraw.h"
#include "GearTemplate.h"

#include <map>
#include <string>

class Player;

// The gear templates of the bots: the files `<Bots.DataDir>/gear/*.txt`. The
// file format and the draw are in the decision library (GearTemplate.h and
// GearDraw.h). This class reads the files and checks the items against the
// item data of the realm.
class BotGear
{
    public:
        // Reads every template file of the folder. A file that the library
        // refuses is left out with an error in the log. So is an item whose
        // entry is not in `item_template` or does not fit its slot.
        void Load(std::string const& gearDir);

        // A template by name, in lower case. Null if there is none.
        Bots::GearTemplate const* Find(std::string const& name) const;

        std::size_t Count() const { return m_templates.size(); }

    private:
        std::map<std::string, Bots::GearTemplate> m_templates;
};

// Equips a character that is in the world from a gear template: it destroys
// every equipped item, then equips the draw of the template for the
// character's GUID, level, and the realm's content patch. It does not check
// skills or the required level: a GM asked for this gear. Saves the
// character when saving is on for it.
Bots::GearDraw EquipFromGearTemplate(Player* player, Bots::GearTemplate const& gearTemplate);

// One line per pick for the GM and the log, for example "head 1234 Cap".
std::string DescribeGearDraw(Bots::GearDraw const& draw);

#endif
