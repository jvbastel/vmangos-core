-- A stub of the 1.12 client API that loads the RouteRecorder addon in a
-- Lua 5.0 interpreter, clicks its buttons, feeds it server lines, and checks
-- what it sends and shows. Run by wowai/tests/route-recorder-addon.test.sh:
--   lua harness.lua <addon folder>
local folder = arg[1]
local failures = 0
local function check(name, ok, detail)
	if ok then
		print("PASS " .. name)
	else
		failures = failures + 1
		print("FAIL " .. name .. " " .. (detail or ""))
	end
end

-- The client API, as far as the addon uses it.
sent = {}
function SendChatMessage(text, kind) table.insert(sent, kind .. " " .. text) end
local globals = {}
fontStrings = {}
function getglobal(name) return globals[name] end
local function NewObject(name)
	local object = { scripts = {}, shown = true, text = "" }
	if name then globals[name] = object end
	function object:SetScript(kind, f) self.scripts[kind] = f end
	function object:GetScript(kind) return self.scripts[kind] end
	function object:SetText(t) self.text = t end
	function object:GetText() return self.text end
	function object:GetName() return name end
	function object:Show() self.shown = true end
	function object:Hide() self.shown = false end
	function object:IsShown() return self.shown end
	function object:RegisterEvent(e) self.events = self.events or {}; self.events[e] = true end
	function object:CreateFontString(n)
		local text = NewObject(n)
		table.insert(fontStrings, text)
		return text
	end
	local noop = function() end
	for _, method in ipairs({"SetWidth", "SetHeight", "SetPoint", "SetBackdrop", "SetMovable", "EnableMouse",
		"RegisterForDrag", "StartMoving", "StopMovingOrSizing", "SetJustifyH", "SetJustifyV", "HighlightText",
		"SetFocus"}) do
		object[method] = noop
	end
	return object
end
local frames = {}
function CreateFrame(kind, name, parent, template)
	local f = NewObject(name)
	f.parent = parent
	function f:GetParent() return self.parent end
	table.insert(frames, f)
	return f
end
UIParent = NewObject("UIParent")
DEFAULT_CHAT_FRAME = NewObject("ChatFrame1")
StaticPopupDialogs = {}
chatShown = {}
function ChatFrame_OnEvent(e) table.insert(chatShown, arg1) end
SlashCmdList = {}
local popup = NewObject("StaticPopup1")
local popupBox = NewObject("StaticPopup1EditBox")
popupBox.parent = popup
function popupBox:GetParent() return self.parent end
local acceptButton = NewObject("StaticPopup1Button1")
acceptButton.parent = popup
function acceptButton:GetParent() return self.parent end
function StaticPopup_Show(which)
	this = popup
	StaticPopupDialogs[which].OnShow()
	popup.which = which
end

local function Fire(e, a1)
	for _, f in ipairs(frames) do
		if f.events and f.events[e] and f.scripts.OnEvent then
			event = e
			arg1 = a1
			this = f
			f.scripts.OnEvent()
		end
	end
end
local function Click(name)
	this = globals[name]
	globals[name].scripts.OnClick()
end
-- A system line reaches the addon and each chat frame.
local function System(line)
	Fire("CHAT_MSG_SYSTEM", line)
	event = "CHAT_MSG_SYSTEM"
	arg1 = line
	this = DEFAULT_CHAT_FRAME
	ChatFrame_OnEvent(event)
end
local function Last() return sent[table.getn(sent)] end

-- The toc loads these files.
local toc = io.open(folder .. "/RouteRecorder.toc")
for line in toc:lines() do
	if not string.find(line, "^##") and string.find(line, "%S") then
		dofile(folder .. "/" .. line)
	end
end
toc:close()

RouteRecorderDB = nil
Fire("VARIABLES_LOADED")
check("the frame exists", globals.RouteRecorderFrame ~= nil)
local status = globals.RouteRecorderFrame
check("the slash command /rr exists", SLASH_ROUTERECORDER1 == "/rr" and SlashCmdList.ROUTERECORDER ~= nil)
check("the key binding names exist", BINDING_NAME_ROUTERECORDER_CHECKPOINT ~= nil)

-- Start asks for the route name.
Click("RouteRecorderStart")
check("Start opens the name dialog", popup.which == "ROUTERECORDER_START")
popupBox:SetText("ragefire_chasm")
this = acceptButton
StaticPopupDialogs.ROUTERECORDER_START.OnAccept()
check("Start sends the start command", Last() == "SAY .route trace start ragefire_chasm", Last())
check("Start remembers the route", RouteRecorderDB.lastRoute == "ragefire_chasm")
StaticPopup_Show("ROUTERECORDER_START")
check("the dialog shows the last route", popupBox:GetText() == "ragefire_chasm", popupBox:GetText())

-- The frame shows what the server answers.
local function Texts()
	local all = ""
	for _, f in ipairs(fontStrings) do
		all = all .. "|" .. (f.text or "")
	end
	return all
end
System("[route] trace on ragefire_chasm points=212 marks=9 length=640y")
check("the frame shows the state and route", string.find(Texts(), "Trace: on   Route: ragefire_chasm", 1, true) ~= nil, Texts())
check("the frame shows the counts", string.find(Texts(), "Points: 212   Checkpoints: 9   Length: 640y", 1, true) ~= nil, Texts())

Click("RouteRecorderMark")
check("Checkpoint sends mark", Last() == "SAY .route trace mark", Last())
RouteRecorder_Checkpoint()
check("the key binding sends mark", Last() == "SAY .route trace mark", Last())
Click("RouteRecorderPause")
check("Pause sends pause", Last() == "SAY .route trace pause", Last())
System("[route] trace paused ragefire_chasm points=213 marks=10 length=644y")
check("the button reads Resume when paused", globals.RouteRecorderPause.text == "Resume")
Click("RouteRecorderPause")
check("Resume sends resume", Last() == "SAY .route trace resume", Last())
System("[route] trace on ragefire_chasm points=214 marks=10 length=644y")
System("[route] trace warning: a gap of 22y from the pause: the trace does not join it")
check("the frame shows a warning", string.find(Texts(), "a gap of 22y", 1, true) ~= nil, Texts())
check("the button reads Pause when on", globals.RouteRecorderPause.text == "Pause")
Click("RouteRecorderUndo")
check("Undo sends undo", Last() == "SAY .route trace undo", Last())

-- The refresh while on asks for the state, and keeps the answer out of the chat.
local before = table.getn(sent)
this = status
arg1 = 6
status.scripts.OnUpdate()
check("the refresh sends show", Last() == "SAY .route trace show" and table.getn(sent) == before + 1, Last())
local shownBefore = table.getn(chatShown)
System("[route] trace on ragefire_chasm points=220 marks=10 length=670y")
check("the refresh answer stays out of the chat", table.getn(chatShown) == shownBefore)
System("[route] trace on ragefire_chasm points=221 marks=10 length=674y")
check("a later answer is in the chat", table.getn(chatShown) == shownBefore + 1)

Click("RouteRecorderStop")
check("Stop sends stop", Last() == "SAY .route trace stop", Last())
System("[route] trace off ragefire_chasm points=221 marks=10 length=674y")
check("the frame shows off", string.find(Texts(), "Trace: off", 1, true) ~= nil, Texts())
System("[route] trace error: no trace is on. Use .route trace start <route> first.")
check("the frame shows an error", string.find(Texts(), "no trace is on", 1, true) ~= nil, Texts())

SlashCmdList.ROUTERECORDER("")
check("/rr hides the frame", not status:IsShown())
SlashCmdList.ROUTERECORDER("")
check("/rr shows the frame and asks for the state", status:IsShown() and Last() == "SAY .route trace show", Last())

if failures > 0 then
	print(failures .. " checks failed")
	os.exit(1)
end
print("all checks passed")
