-- A stub of the 1.12 client API that loads the WowaiRole add-on in a Lua 5.0
-- interpreter, clicks its role buttons, fires the group events, and checks
-- the party add-on messages it sends (jvbastel/WOWAI#197). Run by
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
function SendAddonMessage(prefix, text, kind) table.insert(sent, kind .. " " .. prefix .. "\t" .. text) end
function getglobal(name) return globals[name] end
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
	function object:LockHighlight() self.highlight = true end
	function object:UnlockHighlight() self.highlight = false end
	function object:GetLeft() return 100 end
	function object:GetTop() return 500 end
	function object:RegisterEvent(e) self.events = self.events or {}; self.events[e] = true end
	function object:CreateFontString(n) return NewObject(n) end
	local noop = function() end
	for _, method in ipairs({"SetWidth", "SetHeight", "SetPoint", "SetBackdrop", "SetMovable", "EnableMouse",
		"RegisterForDrag", "StartMoving", "StopMovingOrSizing", "SetTextColor"}) do
		object[method] = noop
	end
	return object
end
function CreateFrame(kind, name, parent, template)
	local f = NewObject(name)
	if name and template == "UIPanelButtonTemplate" then
		NewObject(name .. "Text")
	end
	table.insert(frames, f)
	return f
end

local function Fire(e)
	for _, f in ipairs(frames) do
		if f.events and f.events[e] and f.scripts.OnEvent then
			event = e
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
local function Login(lateVariables)
	globals, frames, sent = {}, {}, {}
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

if failures > 0 then
	print(failures .. " checks failed")
	os.exit(1)
end
print("all checks passed")
