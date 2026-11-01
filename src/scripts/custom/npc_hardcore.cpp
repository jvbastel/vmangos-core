/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// The hardcore NPC (spec jvbastel/WOWAI#250, WOWAI#251): the gossip of creature 190250, script npc_hardcore.
// The world migration 20261101120100_world.sql adds the creature, its texts and its spawns.
//
// Taking the status: the greeting offers it only to a level 1 character with 0 XP that was never hardcore
// (Hardcore::CanTakeStatus). The option opens a page with the rules (final death, no help from non-hardcore
// characters, no way back after a drop) and the confirmation. Each step asks CanTakeStatus again, so a
// character that got XP while the menu was open cannot take it.
//
// Dropping the status (WOWAI#255): the greeting offers it only to a hardcore character that is alive
// (Hardcore::CanDropStatus). The option opens a warning page (a dropped status never comes back) and the
// confirmation; the confirmation drops the status of the player and of every hardcore member of its group
// (Hardcore::DropStatus). The drop option's text has no "hardcore" in it: the checks find the take option
// by that word.
//
// The NPC stands in the 6 starting areas and the 6 capitals (world migration 20261101125500_world.sql). In
// a capital it only offers the drop: its own greeting, and never the take option.

#include "scriptPCH.h"
#include "custom.h"
#include "Hardcore/Hardcore.h"

namespace
{
    // npc_text ids (world migrations 20261101120100_world.sql and 20261101125500_world.sql).
    uint32 const TEXT_GREETING         = 190250;
    uint32 const TEXT_TAKE_RULES       = 190251;
    uint32 const TEXT_CAPITAL_GREETING = 190252;
    uint32 const TEXT_DROP_WARNING     = 190253;

    // Gossip actions.
    uint32 const ACTION_TAKE_RULES   = GOSSIP_ACTION_INFO_DEF + 1;
    uint32 const ACTION_TAKE_CONFIRM = GOSSIP_ACTION_INFO_DEF + 2;
    uint32 const ACTION_CLOSE        = GOSSIP_ACTION_INFO_DEF + 3;
    uint32 const ACTION_DROP_WARNING = GOSSIP_ACTION_INFO_DEF + 10;
    uint32 const ACTION_DROP_CONFIRM = GOSSIP_ACTION_INFO_DEF + 11;

    // The zones (AreaTable) of the 6 capitals.
    uint32 const CAPITAL_ZONES[] = {
        1519, // Stormwind City
        1537, // Ironforge
        1657, // Darnassus
        1637, // Orgrimmar
        1638, // Thunder Bluff
        1497, // Undercity
    };

    bool InCapital(Creature const* creature)
    {
        uint32 const zone = creature->GetZoneId();
        for (uint32 capital : CAPITAL_ZONES)
            if (zone == capital)
                return true;
        return false;
    }

    // The take option: in a starting area only, to a character that may take the status.
    bool OffersTake(Player const* player, Creature const* creature)
    {
        return !InCapital(creature) && Hardcore::CanTakeStatus(player);
    }

    void SendGreeting(Player* player, Creature* creature)
    {
        if (OffersTake(player, creature))
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "I want to become a hardcore character.", GOSSIP_SENDER_MAIN, ACTION_TAKE_RULES);
        if (Hardcore::CanDropStatus(player))
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "I want to drop my status and be a normal character.", GOSSIP_SENDER_MAIN, ACTION_DROP_WARNING);
        player->SEND_GOSSIP_MENU(InCapital(creature) ? TEXT_CAPITAL_GREETING : TEXT_GREETING, creature->GetObjectGuid());
    }

    void SendDropWarning(Player* player, Creature* creature)
    {
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "Yes. Drop my status. I know that it never comes back.", GOSSIP_SENDER_MAIN, ACTION_DROP_CONFIRM);
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "No, I keep it.", GOSSIP_SENDER_MAIN, ACTION_CLOSE);
        player->SEND_GOSSIP_MENU(TEXT_DROP_WARNING, creature->GetObjectGuid());
    }

    void SendTakeRules(Player* player, Creature* creature)
    {
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "Yes. Make me a hardcore character. I accept the rules.", GOSSIP_SENDER_MAIN, ACTION_TAKE_CONFIRM);
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "No, not now.", GOSSIP_SENDER_MAIN, ACTION_CLOSE);
        player->SEND_GOSSIP_MENU(TEXT_TAKE_RULES, creature->GetObjectGuid());
    }
}

bool GossipHello_npc_hardcore(Player* player, Creature* creature)
{
    SendGreeting(player, creature);
    return true;
}

bool GossipSelect_npc_hardcore(Player* player, Creature* creature, uint32 /*sender*/, uint32 action)
{
    player->PlayerTalkClass->ClearMenus();
    switch (action)
    {
        case ACTION_TAKE_RULES:
            if (OffersTake(player, creature))
                SendTakeRules(player, creature);
            else
                SendGreeting(player, creature);
            return true;
        case ACTION_TAKE_CONFIRM:
            player->CLOSE_GOSSIP_MENU();
            if (!InCapital(creature) && Hardcore::TakeStatus(player))
                player->SendSysMessage("You are a hardcore character now. Hardcore: alive");
            else
                player->SendSysMessage("You cannot take the hardcore status: only a level 1 character with no experience that was never hardcore can.");
            return true;
        case ACTION_DROP_WARNING:
            if (Hardcore::CanDropStatus(player))
                SendDropWarning(player, creature);
            else
                SendGreeting(player, creature);
            return true;
        case ACTION_DROP_CONFIRM:
        {
            player->CLOSE_GOSSIP_MENU();
            uint32 const dropped = Hardcore::DropStatus(player);
            if (dropped == 0)
                player->SendSysMessage("You cannot drop the hardcore status: only a hardcore character that is alive can.");
            else if (dropped == 1)
                player->SendSysMessage("You dropped the hardcore status. This character is a normal character now, and can never take it again.");
            else
                player->PSendSysMessage("You dropped the hardcore status, and %u members of your group dropped it with you. "
                    "This character is a normal character now, and can never take it again.", dropped - 1);
            return true;
        }
        case ACTION_CLOSE:
        default:
            player->CLOSE_GOSSIP_MENU();
            return true;
    }
}

void AddSC_npc_hardcore()
{
    Script* newscript = new Script;
    newscript->Name = "npc_hardcore";
    newscript->pGossipHello = &GossipHello_npc_hardcore;
    newscript->pGossipSelect = &GossipSelect_npc_hardcore;
    newscript->RegisterSelf(false);
}
