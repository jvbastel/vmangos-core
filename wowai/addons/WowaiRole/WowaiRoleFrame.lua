-- The frame of WowaiRole: a small movable box with one button per role.
-- A click on the picked role clears the pick. The "-" button folds the box
-- to its title bar. The place and the fold are saved per character.
--
-- To add a section (the bot panel, #198), call in a later file of the toc:
--   WowaiRole.AddSection(height, function(section) ... end)
-- `section` is a Frame of the given height under the sections above it. The
-- frame grows to hold it and hides it with the rest when folded. The builder
-- runs at VARIABLES_LOADED, when the frame is made. AddSection returns the
-- section: WowaiRole.SetSectionHeight(section, height) changes its height.

local WIDTH = 236
local TITLE_HEIGHT = 26
local ROLE_HEIGHT = 30

local frame, foldButton
local roleButtons = {}
local sections = {}   -- { height, build, frame }

function WowaiRole.AddSection(height, build)
	local section = { height = height, build = build }
	table.insert(sections, section)
	if frame then
		WowaiRole.MakeSection(section)
		WowaiRole.Layout()
	end
	return section
end

-- jvbastel/WOWAI#198: a section that grows or shrinks, like the bot panel.
function WowaiRole.SetSectionHeight(section, height)
	section.height = height
	if section.frame then
		section.frame:SetHeight(height)
		WowaiRole.Layout()
	end
end

local function Refresh()
	local picked = WowaiRole.GetRole()
	for key, button in pairs(roleButtons) do
		if key == picked then
			button:LockHighlight()
			getglobal(button:GetName() .. "Text"):SetTextColor(0.2, 1, 0.2)
		else
			button:UnlockHighlight()
			getglobal(button:GetName() .. "Text"):SetTextColor(1, 0.82, 0)
		end
	end
end

function WowaiRole.Layout()
	local folded = WowaiRoleDB.folded
	local height = TITLE_HEIGHT
	if not folded then
		height = height + ROLE_HEIGHT
	end
	for _, section in ipairs(sections) do
		if folded then
			section.frame:Hide()
		else
			section.frame:SetPoint("TOPLEFT", frame, "TOPLEFT", 0, -height)
			section.frame:Show()
			height = height + section.height
		end
	end
	for _, button in pairs(roleButtons) do
		if folded then
			button:Hide()
		else
			button:Show()
		end
	end
	if folded then
		foldButton:SetText("+")
	else
		foldButton:SetText("-")
	end
	frame:SetHeight(height + 6)
end

function WowaiRole.MakeSection(section)
	section.frame = CreateFrame("Frame", nil, frame)
	section.frame:SetWidth(WIDTH)
	section.frame:SetHeight(section.height)
	section.build(section.frame)
end

function WowaiRole.MakeFrame()
	frame = CreateFrame("Frame", "WowaiRoleFrame", UIParent)
	WowaiRole.frame = frame
	frame:SetWidth(WIDTH)
	frame:SetBackdrop({
		bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
		edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
		tile = true, tileSize = 16, edgeSize = 16,
		insets = { left = 4, right = 4, top = 4, bottom = 4 },
	})
	local place = WowaiRoleDB.place
	if place then
		frame:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", place.x, place.y)
	else
		frame:SetPoint("TOPLEFT", UIParent, "TOPLEFT", 260, -120)
	end
	frame:SetMovable(true)
	frame:EnableMouse(true)
	frame:RegisterForDrag("LeftButton")
	frame:SetScript("OnDragStart", function() this:StartMoving() end)
	frame:SetScript("OnDragStop", function()
		this:StopMovingOrSizing()
		WowaiRoleDB.place = { x = this:GetLeft(), y = this:GetTop() }
	end)

	local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	title:SetPoint("TOPLEFT", frame, "TOPLEFT", 10, -9)
	title:SetText("WOWAI role")

	foldButton = CreateFrame("Button", "WowaiRoleFold", frame, "UIPanelButtonTemplate")
	foldButton:SetWidth(20)
	foldButton:SetHeight(18)
	foldButton:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -6, -5)
	foldButton:SetScript("OnClick", function()
		if WowaiRoleDB.folded then
			WowaiRoleDB.folded = nil
		else
			WowaiRoleDB.folded = 1
		end
		WowaiRole.Layout()
	end)

	local x = 8
	for _, role in ipairs(WowaiRole.ROLES) do
		local key = role.key
		local button = CreateFrame("Button", "WowaiRole_" .. key, frame, "UIPanelButtonTemplate")
		button:SetWidth(54)
		button:SetHeight(22)
		button:SetPoint("TOPLEFT", frame, "TOPLEFT", x, -TITLE_HEIGHT)
		button:SetText(role.label)
		button:SetScript("OnClick", function()
			if WowaiRole.GetRole() == key then
				WowaiRole.SetRole("none")
			else
				WowaiRole.SetRole(key)
			end
		end)
		roleButtons[key] = button
		x = x + 55
	end

	for _, section in ipairs(sections) do
		WowaiRole.MakeSection(section)
	end
	WowaiRole.Layout()
	WowaiRole.OnChange(Refresh)
end
