-- The frame of WowaiRole: a small movable box with one button per role.
-- A click on the picked role clears the pick. The frame is hidden at login:
-- a click on the minimap button, or /wowai, shows or hides it. The "x"
-- button and Escape hide it. A drag moves the minimap button around the
-- minimap. The place of the frame and of the button are saved per character.
--
-- To add a section (the bot panel, #198), call in a later file of the toc:
--   WowaiRole.AddSection(height, function(section) ... end)
-- `section` is a Frame of the given height under the sections above it. The
-- frame grows to hold it. The builder runs at VARIABLES_LOADED, when the
-- frame is made. AddSection returns the section:
-- WowaiRole.SetSectionHeight(section, height) changes its height.

local WIDTH = 236
local TITLE_HEIGHT = 26
local ROLE_HEIGHT = 30
-- The minimap button sits on a circle of this radius around the minimap's
-- center, at WowaiRoleDB.minimapAngle degrees (0 is right, 90 is up).
local MINIMAP_RADIUS = 80
local MINIMAP_ANGLE = 200

local frame
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
	local height = TITLE_HEIGHT + ROLE_HEIGHT
	for _, section in ipairs(sections) do
		section.frame:SetPoint("TOPLEFT", frame, "TOPLEFT", 0, -height)
		height = height + section.height
	end
	frame:SetHeight(height + 6)
end

-- Shows the frame if it is hidden, hides it if it is shown.
function WowaiRole.Toggle()
	if frame:IsShown() then
		frame:Hide()
	else
		frame:Show()
	end
end

function WowaiRole.MakeSection(section)
	section.frame = CreateFrame("Frame", nil, frame)
	section.frame:SetWidth(WIDTH)
	section.frame:SetHeight(section.height)
	section.build(section.frame)
end

local function PlaceMinimapButton(button)
	local angle = math.rad(WowaiRoleDB.minimapAngle or MINIMAP_ANGLE)
	button:ClearAllPoints()
	button:SetPoint("CENTER", Minimap, "CENTER",
		MINIMAP_RADIUS * math.cos(angle), MINIMAP_RADIUS * math.sin(angle))
end

-- While a drag: the angle of the cursor around the minimap's center.
local function FollowCursor()
	local x, y = GetCursorPosition()
	local scale = Minimap:GetEffectiveScale()
	local centerX, centerY = Minimap:GetCenter()
	WowaiRoleDB.minimapAngle = math.deg(math.atan2(y / scale - centerY, x / scale - centerX))
	PlaceMinimapButton(this)
end

function WowaiRole.MakeMinimapButton()
	local button = CreateFrame("Button", "WowaiRoleMinimapButton", Minimap)
	button:SetWidth(31)
	button:SetHeight(31)
	button:SetFrameStrata("MEDIUM")
	button:SetFrameLevel(8)
	button:SetHighlightTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight")

	local icon = button:CreateTexture(nil, "BACKGROUND")
	icon:SetTexture("Interface\\Icons\\Ability_Warrior_BattleShout")
	icon:SetWidth(20)
	icon:SetHeight(20)
	icon:SetPoint("TOPLEFT", button, "TOPLEFT", 7, -5)
	local border = button:CreateTexture(nil, "OVERLAY")
	border:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
	border:SetWidth(53)
	border:SetHeight(53)
	border:SetPoint("TOPLEFT", button, "TOPLEFT", 0, 0)

	button:RegisterForClicks("LeftButtonUp")
	button:RegisterForDrag("LeftButton")
	button:SetScript("OnClick", WowaiRole.Toggle)
	button:SetScript("OnDragStart", function() this:SetScript("OnUpdate", FollowCursor) end)
	button:SetScript("OnDragStop", function() this:SetScript("OnUpdate", nil) end)
	button:SetScript("OnEnter", function()
		GameTooltip:SetOwner(this, "ANCHOR_LEFT")
		GameTooltip:SetText("WOWAI role")
		GameTooltip:AddLine("Click to show or hide. Drag to move.", 1, 1, 1)
		GameTooltip:Show()
	end)
	button:SetScript("OnLeave", function() GameTooltip:Hide() end)
	PlaceMinimapButton(button)
end

function WowaiRole.MakeFrame()
	frame = CreateFrame("Frame", "WowaiRoleFrame", UIParent)
	WowaiRole.frame = frame
	frame:Hide()
	-- Escape hides the frames of this list.
	table.insert(UISpecialFrames, "WowaiRoleFrame")
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

	local closeButton = CreateFrame("Button", "WowaiRoleClose", frame, "UIPanelButtonTemplate")
	closeButton:SetWidth(20)
	closeButton:SetHeight(18)
	closeButton:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -6, -5)
	closeButton:SetText("x")
	closeButton:SetScript("OnClick", function() frame:Hide() end)

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
	WowaiRole.MakeMinimapButton()
end

SLASH_WOWAIROLE1 = "/wowai"
SlashCmdList["WOWAIROLE"] = function()
	if frame then
		WowaiRole.Toggle()
	end
end
