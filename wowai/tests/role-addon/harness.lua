-- A stub of the 1.12 client API that loads the WowaiRole add-on in a Lua 5.0
-- interpreter, clicks its role buttons, fires the group events, and checks
-- the party add-on messages it sends (jvbastel/WOWAI#197). It also feeds the
-- bot panel (WowaiBots.lua, #198) a list as system messages, clicks its
-- buttons, and checks what the chat frames show. Run by
-- wowai/tests/role-addon.test.sh:
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

-- The client API, as far as the add-on uses it.
local globals, frames, sent
local partySize = 0
function GetNumPartyMembers() return partySize end
function GetNumRaidMembers() return 0 end
-- `sent`: the role choice messages.
function SendAddonMessage(prefix, text, kind)
	table.insert(sent, kind .. " " .. prefix .. "\t" .. text)
end
function getglobal(name) return globals[name] end
local whispers
function SendChatMessage(text, kind, language, target) table.insert(whispers, kind .. " " .. (target or "") .. " " .. text) end
function UnitName(unit) if unit == "player" then return "Thrallson" end end
local function NewObject(name)
	local object = { scripts = {}, shown = true, text = "", highlight = false }
	if name then globals[name] = object end
	function object:SetScript(kind, f) self.scripts[kind] = f end
	function object:SetText(t) self.text = t end
	function object:GetText() return self.text end
	function object:GetName() return name end
	function object:Show() self.shown = true end
	function object:Hide() self.shown = false end
	function object:IsShown() return self.shown end
	function object:IsVisible() return self.shown and (not self.parent or self.parent:IsVisible()) end
	function object:LockHighlight() self.highlight = true end
	function object:UnlockHighlight() self.highlight = false end
	function object:GetLeft() return 100 end
	function object:GetTop() return 500 end
	function object:RegisterEvent(e) self.events = self.events or {}; self.events[e] = true end
	function object:CreateFontString(n) local f = NewObject(n); f.parent = self; return f end
	function object:SetHeight(h) self.height = h end
	local noop = function() end
	for _, method in ipairs({"SetWidth", "SetPoint", "SetBackdrop", "SetMovable", "EnableMouse",
		"RegisterForDrag", "StartMoving", "StopMovingOrSizing", "SetTextColor"}) do
		object[method] = noop
	end
	return object
end
function CreateFrame(kind, name, parent, template)
	local f = NewObject(name)
	f.parent = parent
	if name and template == "UIPanelButtonTemplate" then
		NewObject(name .. "Text")
	end
	table.insert(frames, f)
	return f
end

local function Fire(e, a1, a2, a3, a4)
	for _, f in ipairs(frames) do
		if f.events and f.events[e] and f.scripts.OnEvent then
			event = e
			arg1, arg2, arg3, arg4 = a1, a2, a3, a4
			this = f
			f.scripts.OnEvent()
		end
	end
end
local function Click(name)
	this = globals[name]
	globals[name].scripts.OnClick()
end
local function Last() return sent[table.getn(sent)] end

-- A login: the client loads the toc files, then the saved variables.
-- `lateVariables`: VARIABLES_LOADED after PLAYER_ENTERING_WORLD.
-- The chat frames of FrameXML: ChatFrame_OnEvent(event) shows a line.
local chatShown
local function FrameXMLChatFrame(e) table.insert(chatShown, e .. " " .. tostring(arg1)) end
local function Login(lateVariables)
	globals, frames, sent, whispers, chatShown = {}, {}, {}, {}, {}
	ChatFrame_OnEvent = FrameXMLChatFrame
	UIParent = NewObject("UIParent")
	WowaiRole = nil
	local toc = io.open(folder .. "/WowaiRole.toc")
	for line in toc:lines() do
		if not string.find(line, "^##") and string.find(line, "%S") then
			dofile(folder .. "/" .. line)
		end
	end
	toc:close()
	if lateVariables then
		Fire("PLAYER_ENTERING_WORLD")
		Fire("VARIABLES_LOADED")
	else
		Fire("VARIABLES_LOADED")
		Fire("PLAYER_ENTERING_WORLD")
	end
end

WowaiRoleDB = nil
Login()
check("the frame exists", globals.WowaiRoleFrame ~= nil)
check("a button for each role", globals.WowaiRole_tank and globals.WowaiRole_healer and globals.WowaiRole_melee
	and globals.WowaiRole_ranged)
check("nothing is sent with no group and no pick", table.getn(sent) == 0)

Click("WowaiRole_tank")
check("a pick is sent, also with no group", Last() == "PARTY WOWAI_ROLE\t1:tank", Last())
check("the pick is saved per character", WowaiRoleDB.role == "tank")
check("the picked button is lit", globals.WowaiRole_tank.highlight and not globals.WowaiRole_healer.highlight)

partySize = 1
Fire("PARTY_MEMBERS_CHANGED")
check("joining a group sends the pick", Last() == "PARTY WOWAI_ROLE\t1:tank", Last())

Click("WowaiRole_healer")
check("a new pick is sent", Last() == "PARTY WOWAI_ROLE\t1:healer", Last())
Click("WowaiRole_healer")
check("a click on the pick clears it", Last() == "PARTY WOWAI_ROLE\t1:none" and WowaiRoleDB.role == nil, Last())
Click("WowaiRole_tank")

-- Logout and login: the saved variables stay.
partySize = 0
Login()
check("the pick survives a logout", WowaiRole.GetRole() == "tank" and globals.WowaiRole_tank.highlight)
check("a pick is sent at login, also with no group", Last() == "PARTY WOWAI_ROLE\t1:tank", Last())
partySize = 2
Fire("PARTY_MEMBERS_CHANGED")
check("the pick is sent again on joining a group", Last() == "PARTY WOWAI_ROLE\t1:tank", Last())
Login()
check("a login in a group sends the pick", Last() == "PARTY WOWAI_ROLE\t1:tank", Last())

-- In 1.12 VARIABLES_LOADED can come after PLAYER_ENTERING_WORLD.
partySize = 0
Login(true)
check("a login with late saved variables sends the pick, with no group", Last() == "PARTY WOWAI_ROLE\t1:tank", Last())

-- The fold, and a later section (the bot panel of #198).
local built = false
WowaiRole.AddSection(40, function(section) built = section ~= nil end)
check("a section can be added to the frame", built)
Click("WowaiRoleFold")
check("the fold hides the role buttons", not globals.WowaiRole_tank:IsShown() and WowaiRoleDB.folded == 1)
Click("WowaiRoleFold")
check("the unfold shows them", globals.WowaiRole_tank:IsShown() and WowaiRoleDB.folded == nil)


-- jvbastel/WOWAI#198: the bot panel.
local REQUEST = "WHISPER Thrallson WOWAI_BOTS 1:list"
local function LastWhisper() return whispers[table.getn(whispers)] end
local function Requests()
	local n = 0
	for _, line in ipairs(whispers) do
		if line == REQUEST then n = n + 1 end
	end
	return n
end
-- The server's answer: system messages, through the add-on's events and
-- the chat frames, as the client gives them.
local function Answer(texts)
	for _, text in ipairs(texts) do
		Fire("CHAT_MSG_SYSTEM", "WOWAI_BOTS " .. text)
		arg1 = "WOWAI_BOTS " .. text
		ChatFrame_OnEvent("CHAT_MSG_SYSTEM")
	end
end
local HORDE = { "1:clear", "1:bot:tank:Grukk", "1:bot:healer:Hela", "1:bot:melee:Shanka", "1:bot:ranged:Ezzra",
	"1:bot:ranged:Morzul", "1:end" }
local function Row(i)
	return getglobal("WowaiBotsRole" .. i):GetText(), getglobal("WowaiBotsName" .. i):GetText(),
		getglobal("WowaiBotsName" .. i):IsShown()
end

WowaiRoleDB = nil
partySize = 0
Login()
check("the panel asks for the list at login, with no group, by a whisper to the player", Requests() == 1,
	tostring(LastWhisper()))
check("no rows before the list comes", getglobal("WowaiBotsName1") == nil or not getglobal("WowaiBotsName1"):IsShown())
local heightBefore = WowaiRole.frame.height
Answer(HORDE)
local role, name, shown = Row(1)
check("the first row is the tank", role == "Tank" and name == "Grukk" and shown, tostring(role) .. " " .. tostring(name))
role, name = Row(4)
check("the ranged group starts with its role label", role == "Ranged" and name == "Ezzra", tostring(role) .. " " .. tostring(name))
role, name = Row(5)
check("a second bot of a role has no label", role == "" and name == "Morzul", tostring(role) .. " " .. tostring(name))
check("the frame grows to hold the rows", WowaiRole.frame.height > heightBefore,
	tostring(heightBefore) .. " -> " .. tostring(WowaiRole.frame.height))
check("the chat frames do not show the panel's messages", table.getn(chatShown) == 0, tostring(chatShown[1]))
arg1 = "You are now AFK."
ChatFrame_OnEvent("CHAT_MSG_SYSTEM")
check("the chat frames still show other system messages", chatShown[1] == "CHAT_MSG_SYSTEM You are now AFK.", tostring(chatShown[1]))
local heightFive = WowaiRole.frame.height

Click("WowaiBotsInvite1")
check("Invite whispers invite to the bot", LastWhisper() == "WHISPER Grukk invite", LastWhisper())
Click("WowaiBotsDismiss2")
check("Dismiss whispers dismiss to the bot", LastWhisper() == "WHISPER Hela dismiss", LastWhisper())

Answer({ "1:clear", "1:bot:tank:Aldric", "1:end" })
role, name, shown = Row(1)
check("a new list replaces the old one", name == "Aldric" and not getglobal("WowaiBotsName2"):IsShown())
check("the frame shrinks with the list", WowaiRole.frame.height < heightFive,
	tostring(heightFive) .. " -> " .. tostring(WowaiRole.frame.height))

Answer({ "2:clear", "2:bot:tank:Later", "2:end" })
check("a list of another version is ignored", Row(1) == "Tank" and getglobal("WowaiBotsName1"):GetText() == "Aldric")
Fire("CHAT_MSG_SYSTEM", "WOWAI_BOTSX 1:clear")
check("a message of another prefix is ignored", getglobal("WowaiBotsName1"):GetText() == "Aldric")

Click("WowaiBotsRefresh")
check("Refresh asks for the list again", Requests() == 2 and LastWhisper() == REQUEST, LastWhisper())
partySize = 1
Fire("PARTY_MEMBERS_CHANGED")
check("a group change does not ask again (the list does not depend on the group)", Requests() == 2)

Click("WowaiRoleFold")
check("the fold hides the panel", not getglobal("WowaiBotsRefresh"):IsVisible())
Click("WowaiRoleFold")
check("the unfold shows the panel", getglobal("WowaiBotsRefresh"):IsVisible())

if failures > 0 then
	print(failures .. " checks failed")
	os.exit(1)
end
print("all checks passed")
