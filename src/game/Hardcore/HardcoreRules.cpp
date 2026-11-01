/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*/

// The hardcore border (WOWAI#254, user stories 26-33 of #250): a hardcore character gets no help from
// characters that are not hardcore. No trade, no mail from them, no auction house, and no group with them.
// Each refusal tells the acting character the hardcore rule that blocked it.

#include "Hardcore.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldSession.h"

namespace
{
    char const* const NoTrade = "Hardcore: a hardcore character cannot trade with a character that is not hardcore.";
    char const* const NoMail = "Hardcore: a character that is not hardcore cannot send mail to a hardcore character.";
    char const* const NoCashOnDelivery =
        "Hardcore: a hardcore character cannot send cash on delivery mail to a character that is not hardcore.";
    char const* const NoAuctionHouse = "Hardcore: a hardcore character cannot use the auction house.";
    char const* const NoGroup =
        "Hardcore: a hardcore character cannot be in a group with a character that is not hardcore.";
    char const* const DeadMember = "Hardcore: this character is dead for good. A hardcore death is final.";

    // True when exactly one of the two characters is hardcore.
    bool AcrossBorder(uint32 one, uint32 two)
    {
        return Hardcore::IsHardcore(one) != Hardcore::IsHardcore(two);
    }

    bool AcrossBorder(Player const* one, Player const* two)
    {
        return Hardcore::IsHardcore(one) != Hardcore::IsHardcore(two);
    }
}

char const* Hardcore::GroupRefusal(uint32 joiner, uint32 member)
{
    // A dead member already in the group (its 60 seconds) does not block a new member: it leaves at its
    // logout.
    if (GetStatus(joiner) == Status::Dead)
        return DeadMember;
    if (AcrossBorder(joiner, member))
        return NoGroup;
    return nullptr;
}

bool Hardcore::AllowTrade(Player* player, Player const* other)
{
    if (!AcrossBorder(player, other))
        return true;
    // Client: the reason line only. The trade window opens only at TRADE_STATUS_BEGIN_TRADE, so nothing waits.
    player->SendSysMessage(NoTrade);
    return false;
}

bool Hardcore::AllowMail(Player* sender, uint32 receiverGuid, bool cashOnDelivery)
{
    bool const senderHardcore = IsHardcore(sender);
    bool const receiverHardcore = IsHardcore(receiverGuid);
    char const* reason = nullptr;
    if (!senderHardcore && receiverHardcore)
        reason = NoMail;
    else if (senderHardcore && !receiverHardcore && cashOnDelivery)
        reason = NoCashOnDelivery;
    if (!reason)
        return true;
    // Client: the mail window waits for a send result, so it gets one (an error, nothing is sent), and the
    // reason line.
    sender->GetSession()->SendMailResult(0, MAIL_SEND, MAIL_ERR_INTERNAL_ERROR);
    sender->SendSysMessage(reason);
    return false;
}

bool Hardcore::AllowAuctionHouse(Player* player)
{
    if (!IsHardcore(player))
        return true;
    // Client: no MSG_AUCTION_HELLO answer, so the window does not open; the reason line.
    player->SendSysMessage(NoAuctionHouse);
    return false;
}

bool Hardcore::AllowGroupInvite(Player* inviter, Player const* invited)
{
    char const* reason = GetStatus(inviter) == Status::Dead ? DeadMember
        : GroupRefusal(invited->GetGUIDLow(), inviter->GetGUIDLow());
    if (!reason && IsBot(invited->GetGUIDLow()))
        reason = BotLevelRefusal(inviter, invited->GetGUIDLow());
    if (!reason)
        return true;
    // Client: the reason line only. The invited character gets no invite. No PartyResult of 1.12 names this
    // rule, so none is sent.
    inviter->SendSysMessage(reason);
    return false;
}

bool Hardcore::AllowGroupMember(Group const* group, ObjectGuid const& guid)
{
    // The battleground raid: see Hardcore.h.
    if (!group || group->isBGGroup())
        return true;
    char const* reason = nullptr;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
        if (slot.guid != guid && (reason = GroupRefusal(guid.GetCounter(), slot.guid.GetCounter())))
            break;
    if (!reason)
        return true;
    // Database and client: nothing changes; the stock code sees a failed add. The new member and the leader
    // read the reason, when they are online.
    if (Player* member = ObjectAccessor::FindPlayer(guid))
        member->SendSysMessage(reason);
    if (group->GetLeaderGuid() != guid)
        if (Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGuid()))
            leader->SendSysMessage(reason);
    sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "Hardcore: refused guid %u in the group of leader guid %u: %s", guid.GetCounter(),
        group->GetLeaderGuid().GetCounter(), reason);
    return false;
}
