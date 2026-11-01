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

#include "scriptPCH.h"
#include "custom.h"
#include "Hardcore/Hardcore.h"

namespace
{
    // npc_text ids (world migration 20261101120100_world.sql). 190252 and up are free for the drop (#255).
    uint32 const TEXT_GREETING   = 190250;
    uint32 const TEXT_TAKE_RULES = 190251;

    // Gossip actions. GOSSIP_ACTION_INFO_DEF + 10 and up are free for the drop (#255).
    uint32 const ACTION_TAKE_RULES   = GOSSIP_ACTION_INFO_DEF + 1;
    uint32 const ACTION_TAKE_CONFIRM = GOSSIP_ACTION_INFO_DEF + 2;
    uint32 const ACTION_CLOSE        = GOSSIP_ACTION_INFO_DEF + 3;

    void SendGreeting(Player* player, Creature* creature)
    {
        if (Hardcore::CanTakeStatus(player))
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "I want to become a hardcore character.", GOSSIP_SENDER_MAIN, ACTION_TAKE_RULES);
        player->SEND_GOSSIP_MENU(TEXT_GREETING, creature->GetObjectGuid());
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
            if (Hardcore::CanTakeStatus(player))
                SendTakeRules(player, creature);
            else
                SendGreeting(player, creature);
            return true;
        case ACTION_TAKE_CONFIRM:
            player->CLOSE_GOSSIP_MENU();
            if (Hardcore::TakeStatus(player))
                player->SendSysMessage("You are a hardcore character now. Hardcore: alive");
            else
                player->SendSysMessage("You cannot take the hardcore status: only a level 1 character with no experience that was never hardcore can.");
            return true;
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
