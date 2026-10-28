-- WowaiBots: the bot panel of the WowaiRole add-on (jvbastel/WOWAI#198).
-- For the vanilla 1.12 client (Lua 5.0).
--
-- The panel asks the server for the group bots of the player's faction with
-- a whisper to the player's own name: "WOWAI_BOTS 1:list". The server takes
-- that whisper (nobody gets it) and answers with system messages to the
-- player alone: "WOWAI_BOTS 1:clear", then "WOWAI_BOTS 1:bot:<role>:<name>"
-- per bot in role order, then "WOWAI_BOTS 1:end" (Bots::BotPanelMessages,
-- src/bots/BotPanel.h in the core fork). Both work with no group: the 1.12
-- client sends a party add-on message only in a party. The add-on hides these
-- system messages from the chat frames. The panel asks on entering the world
-- and on a click of Refresh.
--
-- Invite and Dismiss whisper "invite" and "dismiss" to the bot: the same
-- server path as the typed bot commands. The server logs an offline bot in,
-- adds it to the group, brings it to the player, and refuses a second tank.

WowaiBots = {}

WowaiBots.PREFIX = "WOWAI_BOTS"
WowaiBots.VERSION = "1"

local HEADER_HEIGHT = 24
local ROW_HEIGHT = 20
local ROLE_LABELS = { tank = "Tank", healer = "Healer", melee = "Melee", ranged = "Ranged" }

local section, panel, status
local rows = {}      -- the row widgets, made when first needed
local incoming = {}  -- the bots of the list that is coming in
local bots = {}      -- the bots on the panel: { role, name }
local received       -- a whole list came

-- "WOWAI_BOTS 1:", the start of each panel message.
local function Head()
	return WowaiBots.PREFIX .. " " .. WowaiBots.VERSION .. ":"
end

function WowaiBots.Request()
	SendChatMessage(Head() .. "list", "WHISPER", nil, UnitName("player"))
end

-- True for a system message of the panel.
function WowaiBots.IsPanelMessage(text)
	return type(text) == "string" and string.sub(text, 1, string.len(Head())) == Head()
end

function WowaiBots.Invite(name)
	SendChatMessage("invite", "WHISPER", nil, name)
end

function WowaiBots.Dismiss(name)
	SendChatMessage("dismiss", "WHISPER", nil, name)
end

local function MakeRow(i)
	local y = -HEADER_HEIGHT - (i - 1) * ROW_HEIGHT
	local row = {}
	row.role = panel:CreateFontString("WowaiBotsRole" .. i, "OVERLAY", "GameFontNormalSmall")
	row.role:SetPoint("TOPLEFT", panel, "TOPLEFT", 10, y - 4)
	row.name = panel:CreateFontString("WowaiBotsName" .. i, "OVERLAY", "GameFontHighlightSmall")
	row.name:SetPoint("TOPLEFT", panel, "TOPLEFT", 56, y - 4)
	row.invite = CreateFrame("Button", "WowaiBotsInvite" .. i, panel, "UIPanelButtonTemplate")
	row.invite:SetWidth(52)
	row.invite:SetHeight(18)
	row.invite:SetPoint("TOPLEFT", panel, "TOPLEFT", 128, y)
	row.invite:SetText("Invite")
	row.invite:SetScript("OnClick", function() WowaiBots.Invite(bots[i].name) end)
	row.dismiss = CreateFrame("Button", "WowaiBotsDismiss" .. i, panel, "UIPanelButtonTemplate")
	row.dismiss:SetWidth(52)
	row.dismiss:SetHeight(18)
	row.dismiss:SetPoint("TOPLEFT", panel, "TOPLEFT", 180, y)
	row.dismiss:SetText("Dismiss")
	row.dismiss:SetScript("OnClick", function() WowaiBots.Dismiss(bots[i].name) end)
	return row
end

local function ShowRow(row, shown)
	local widgets = { row.role, row.name, row.invite, row.dismiss }
	for _, widget in ipairs(widgets) do
		if shown then
			widget:Show()
		else
			widget:Hide()
		end
	end
end

-- Draws `bots`: one row per bot, the role label on the first row of a role.
function WowaiBots.Render()
	if not panel then
		return -- The frame comes at VARIABLES_LOADED; its builder draws then.
	end
	local count = table.getn(bots)
	local lastRole
	for i, bot in ipairs(bots) do
		if not rows[i] then
			rows[i] = MakeRow(i)
		end
		if bot.role == lastRole then
			rows[i].role:SetText("")
		else
			rows[i].role:SetText(ROLE_LABELS[bot.role] or bot.role)
		end
		lastRole = bot.role
		rows[i].name:SetText(bot.name)
		ShowRow(rows[i], true)
	end
	for i = count + 1, table.getn(rows) do
		ShowRow(rows[i], false)
	end
	if count == 0 and received then
		status:SetText("No bots")
	else
		status:SetText("")
	end
	WowaiRole.SetSectionHeight(section, HEADER_HEIGHT + count * ROW_HEIGHT + 4)
end

-- A system message of the panel, from the server.
function WowaiBots.OnMessage(text)
	if not WowaiBots.IsPanelMessage(text) then
		return
	end
	local body = string.sub(text, string.len(Head()) + 1)
	if body == "clear" then
		incoming = {}
	elseif body == "end" then
		bots = incoming
		incoming = {}
		received = true
		WowaiBots.Render()
	else
		local _, _, role, name = string.find(body, "^bot:(%a+):(%S+)$")
		if role then
			table.insert(incoming, { role = role, name = name })
		end
	end
end

section = WowaiRole.AddSection(HEADER_HEIGHT + 4, function(frame)
	panel = frame
	local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	title:SetPoint("TOPLEFT", frame, "TOPLEFT", 10, -4)
	title:SetText("Bots")
	status = frame:CreateFontString("WowaiBotsStatus", "OVERLAY", "GameFontDisableSmall")
	status:SetPoint("TOPLEFT", frame, "TOPLEFT", 56, -6)
	status:SetText("Asking the server...")
	local refresh = CreateFrame("Button", "WowaiBotsRefresh", frame, "UIPanelButtonTemplate")
	refresh:SetWidth(64)
	refresh:SetHeight(18)
	refresh:SetPoint("TOPLEFT", frame, "TOPLEFT", 168, -2)
	refresh:SetText("Refresh")
	refresh:SetScript("OnClick", WowaiBots.Request)
	if received then
		WowaiBots.Render()
	end
end)

local events = CreateFrame("Frame", "WowaiBotsEvents")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("CHAT_MSG_SYSTEM")
events:SetScript("OnEvent", function()
	if event == "CHAT_MSG_SYSTEM" then
		WowaiBots.OnMessage(arg1)
	else
		WowaiBots.Request()
	end
end)

-- The chat frames do not show the panel's system messages. In 1.12 each chat
-- frame calls the global ChatFrame_OnEvent(event), with arg1 the text.
local ShowInChatFrame = ChatFrame_OnEvent
function ChatFrame_OnEvent(event)
	if event == "CHAT_MSG_SYSTEM" and WowaiBots.IsPanelMessage(arg1) then
		return
	end
	ShowInChatFrame(event)
end
