-- WowaiRole: the role choice of the player, for the WOWAI bots
-- (jvbastel/WOWAI#197). For the vanilla 1.12 client (Lua 5.0).
--
-- The pick is saved per character (WowaiRoleDB.role) and sent to the party
-- as an add-on message: prefix "WOWAI_ROLE", text "1:<role>", with role one
-- of tank, healer, melee, ranged or none. The server reads it with
-- Bots::ParseRoleChoiceMessage (src/bots/RoleChoice.h in the core fork).
-- A new text format takes a new version number on both sides.
--
-- The add-on sends the pick on a new pick (also with no group), when the
-- player joins a group, on each group change, and at login when the player
-- is in a group.
--
-- This file holds the state and the messages. WowaiRoleFrame.lua holds the
-- frame. Other parts of the add-on (the bot panel, #198) add their own file
-- to the toc and their own section to the frame with WowaiRole.AddSection.

WowaiRole = {}

WowaiRole.PREFIX = "WOWAI_ROLE"
WowaiRole.VERSION = "1"
-- The roles, in the order of the frame's buttons, with their labels.
WowaiRole.ROLES = {
	{ key = "tank", label = "Tank" },
	{ key = "healer", label = "Healer" },
	{ key = "melee", label = "Melee" },
	{ key = "ranged", label = "Ranged" },
}

-- Called after each change of the pick, for the frame.
local listeners = {}

function WowaiRole.IsRole(key)
	for _, role in ipairs(WowaiRole.ROLES) do
		if role.key == key then
			return true
		end
	end
	return false
end

-- The saved pick, or "none".
function WowaiRole.GetRole()
	if WowaiRoleDB and WowaiRole.IsRole(WowaiRoleDB.role) then
		return WowaiRoleDB.role
	end
	return "none"
end

function WowaiRole.InGroup()
	return GetNumPartyMembers() > 0 or GetNumRaidMembers() > 0
end

-- The text of the message for `role`.
function WowaiRole.Message(role)
	return WowaiRole.VERSION .. ":" .. role
end

-- Sends the pick to the party. Outside a group only when `solo`: the server
-- reads a party add-on message of a player with no group too, so that it
-- knows the pick before the first invite. The group events do not send
-- outside a group.
function WowaiRole.Send(solo)
	if not solo and not WowaiRole.InGroup() then
		return
	end
	SendAddonMessage(WowaiRole.PREFIX, WowaiRole.Message(WowaiRole.GetRole()), "PARTY")
end

-- A new pick: "tank", "healer", "melee", "ranged", or "none" to clear it.
function WowaiRole.SetRole(key)
	if not WowaiRole.IsRole(key) then
		key = nil
	end
	WowaiRoleDB.role = key
	WowaiRole.Send(true)
	for _, listener in ipairs(listeners) do
		listener()
	end
end

function WowaiRole.OnChange(listener)
	table.insert(listeners, listener)
end

local events = CreateFrame("Frame", "WowaiRoleEvents")
events:RegisterEvent("VARIABLES_LOADED")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("PARTY_MEMBERS_CHANGED")
events:SetScript("OnEvent", function()
	if event == "VARIABLES_LOADED" then
		if not WowaiRoleDB then
			WowaiRoleDB = {}
		end
		WowaiRole.MakeFrame()
		for _, listener in ipairs(listeners) do
			listener()
		end
	elseif event == "PLAYER_ENTERING_WORLD" or event == "PARTY_MEMBERS_CHANGED" then
		if WowaiRoleDB then
			WowaiRole.Send()
		end
	end
end)
