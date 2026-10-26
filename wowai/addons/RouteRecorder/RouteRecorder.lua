-- RouteRecorder: buttons for the GM trace of a WOWAI dungeon route.
-- For the vanilla 1.12 client (Lua 5.0). The addon only sends the
-- `.route trace` GM commands as chat lines. All the logic is on the server.
-- The frame shows what the server answers, in lines like:
--   [route] trace on ragefire_chasm points=212 marks=9 length=640y
--   [route] trace warning: a jump of 54y (a teleport?): the trace leaves it out
--   [route] trace error: no trace is on. Use .route trace start <route> first.
-- See wowai/docs/route-recorder.md in the core fork.

BINDING_HEADER_ROUTERECORDER = "Route Recorder"
BINDING_NAME_ROUTERECORDER_CHECKPOINT = "Checkpoint"
BINDING_NAME_ROUTERECORDER_TOGGLE = "Show or hide the frame"

-- While a trace is on, the addon asks for the state this often (seconds),
-- and keeps those answers out of the chat frame.
local REFRESH = 5

local state = "off"   -- off, on, paused, or unknown
local route = ""
local points = 0
local marks = 0
local length = 0
local note = ""
local sinceRefresh = 0
local quiet = 0       -- answers of our own refreshes to keep out of the chat

local frame, statusText, countText, noteText, pauseButton

local function Send(command)
	SendChatMessage(command, "SAY")
end

local function Update()
	if not frame then
		return
	end
	local name = route
	if name == "" then
		name = "-"
	end
	statusText:SetText("Trace: " .. state .. "   Route: " .. name)
	countText:SetText("Points: " .. points .. "   Checkpoints: " .. marks .. "   Length: " .. length .. "y")
	noteText:SetText(note)
	if state == "paused" then
		pauseButton:SetText("Resume")
	else
		pauseButton:SetText("Pause")
	end
end

-- Reads one system line. True when it is a line of the trace commands.
local function Read(text)
	if not text then
		return false
	end
	local _, _, s, name, p, m, l = string.find(text, "^%[route%] trace (%a+) (%S+) points=(%d+) marks=(%d+) length=(%d+)y")
	if s then
		state = s
		route = name
		points = tonumber(p)
		marks = tonumber(m)
		length = tonumber(l)
		if state == "off" then
			note = "Stopped. The trace file is written."
		end
		Update()
		return true
	end
	local _, _, warning = string.find(text, "^%[route%] trace warning: (.*)")
	if warning then
		note = "|cffffd100" .. warning .. "|r"
		Update()
		return true
	end
	local _, _, err = string.find(text, "^%[route%] trace error: (.*)")
	if err then
		if string.find(err, "^no trace is on") then
			state = "off"
		end
		quiet = 0
		note = "|cffff2020" .. err .. "|r"
		Update()
		return true
	end
	return false
end

function RouteRecorder_Start(name)
	if not name or name == "" then
		return
	end
	RouteRecorderDB.lastRoute = name
	note = ""
	Send(".route trace start " .. name)
end

function RouteRecorder_PauseResume()
	if state == "paused" then
		Send(".route trace resume")
	else
		Send(".route trace pause")
	end
end

function RouteRecorder_Checkpoint()
	Send(".route trace mark")
end

function RouteRecorder_Undo()
	Send(".route trace undo")
end

function RouteRecorder_Stop()
	Send(".route trace stop")
end

function RouteRecorder_Toggle()
	if frame:IsShown() then
		frame:Hide()
		RouteRecorderDB.hidden = 1
	else
		frame:Show()
		RouteRecorderDB.hidden = nil
		quiet = quiet + 1
		Send(".route trace show")
	end
end

StaticPopupDialogs["ROUTERECORDER_START"] = {
	text = "Route name (a-z, 0-9, _):",
	button1 = "Start",
	button2 = "Cancel",
	hasEditBox = 1,
	maxLetters = 40,
	OnShow = function()
		local box = getglobal(this:GetName() .. "EditBox")
		box:SetText(RouteRecorderDB.lastRoute or "")
		box:HighlightText()
		box:SetFocus()
	end,
	OnAccept = function()
		local box = getglobal(this:GetParent():GetName() .. "EditBox")
		RouteRecorder_Start(box:GetText())
	end,
	EditBoxOnEnterPressed = function()
		RouteRecorder_Start(this:GetText())
		this:GetParent():Hide()
	end,
	EditBoxOnEscapePressed = function()
		this:GetParent():Hide()
	end,
	timeout = 0,
	whileDead = 1,
	hideOnEscape = 1,
}

local function MakeButton(name, label, x, y, width, onClick)
	local button = CreateFrame("Button", name, frame, "UIPanelButtonTemplate")
	button:SetWidth(width)
	button:SetHeight(22)
	button:SetPoint("TOPLEFT", frame, "TOPLEFT", x, y)
	button:SetText(label)
	button:SetScript("OnClick", onClick)
	return button
end

local function MakeFrame()
	frame = CreateFrame("Frame", "RouteRecorderFrame", UIParent)
	frame:SetWidth(290)
	frame:SetHeight(122)
	frame:SetPoint("CENTER", UIParent, "CENTER", 0, 200)
	frame:SetBackdrop({
		bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
		edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
		tile = true, tileSize = 16, edgeSize = 16,
		insets = { left = 4, right = 4, top = 4, bottom = 4 },
	})
	frame:SetMovable(true)
	frame:EnableMouse(true)
	frame:RegisterForDrag("LeftButton")
	frame:SetScript("OnDragStart", function() this:StartMoving() end)
	frame:SetScript("OnDragStop", function() this:StopMovingOrSizing() end)

	local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	title:SetPoint("TOPLEFT", frame, "TOPLEFT", 10, -9)
	title:SetText("Route Recorder")

	local close = CreateFrame("Button", "RouteRecorderClose", frame, "UIPanelCloseButton")
	close:SetPoint("TOPRIGHT", frame, "TOPRIGHT", 2, 2)
	close:SetScript("OnClick", function() RouteRecorder_Toggle() end)

	statusText = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
	statusText:SetPoint("TOPLEFT", frame, "TOPLEFT", 10, -27)
	countText = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
	countText:SetPoint("TOPLEFT", frame, "TOPLEFT", 10, -41)
	noteText = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
	noteText:SetPoint("TOPLEFT", frame, "TOPLEFT", 10, -55)
	noteText:SetWidth(270)
	noteText:SetHeight(24)
	noteText:SetJustifyH("LEFT")
	noteText:SetJustifyV("TOP")

	MakeButton("RouteRecorderStart", "Start", 8, -86, 52, function() StaticPopup_Show("ROUTERECORDER_START") end)
	pauseButton = MakeButton("RouteRecorderPause", "Pause", 62, -86, 60, RouteRecorder_PauseResume)
	MakeButton("RouteRecorderMark", "Checkpoint", 124, -86, 74, RouteRecorder_Checkpoint)
	MakeButton("RouteRecorderUndo", "Undo", 200, -86, 40, RouteRecorder_Undo)
	MakeButton("RouteRecorderStop", "Stop", 242, -86, 40, RouteRecorder_Stop)

	frame:SetScript("OnUpdate", function()
		if state ~= "on" and state ~= "paused" then
			return
		end
		sinceRefresh = sinceRefresh + arg1
		if sinceRefresh >= REFRESH then
			sinceRefresh = 0
			quiet = quiet + 1
			Send(".route trace show")
		end
	end)
	Update()
end

-- Keeps the answers of the refreshes out of the chat frame.
local ChatFrameOnEvent = ChatFrame_OnEvent
function ChatFrame_OnEvent(event)
	if event == "CHAT_MSG_SYSTEM" and quiet > 0 and arg1 and string.find(arg1, "^%[route%] trace %a+ %S+ points=") then
		if this == DEFAULT_CHAT_FRAME then
			quiet = quiet - 1
		end
		return
	end
	ChatFrameOnEvent(event)
end

local events = CreateFrame("Frame", "RouteRecorderEvents")
events:RegisterEvent("VARIABLES_LOADED")
events:RegisterEvent("CHAT_MSG_SYSTEM")
events:SetScript("OnEvent", function()
	if event == "VARIABLES_LOADED" then
		if not RouteRecorderDB then
			RouteRecorderDB = {}
		end
		MakeFrame()
		if RouteRecorderDB.hidden then
			frame:Hide()
		end
	elseif event == "CHAT_MSG_SYSTEM" then
		Read(arg1)
	end
end)

SLASH_ROUTERECORDER1 = "/rr"
SlashCmdList["ROUTERECORDER"] = function()
	RouteRecorder_Toggle()
end
