-- SPDX-License-Identifier: MIT
local Model = dofile("lib/expedition_journal_state.lua")
local Guide = dofile("lib/expedition_journal_catalog.lua")
local Library = dofile("lib/expedition_journal_library.lua")
local Featured = dofile("lib/expedition_journal_featured.lua")
local M = {}
local featuredById = {}
for _, entry in ipairs(Featured) do featuredById[entry.id] = entry end

local color = {
  bg = {.071, .075, .086, 1}, rail = {.090, .090, .102, 1},
  chrome = {.125, .125, .137, 1}, card = {.102, .106, .118, 1},
  line = {.229, .203, .214, 1}, text = {.945, .914, .875, 1},
  muted = {.762, .719, .687, 1}, gold = {.949, .780, .533, 1},
  bright = {1, .875, .690, 1}, good = {.655, .847, .718, 1},
  danger = {.945, .655, .585, 1}, active = {.325, .208, .176, 1},
  hover = {.252, .190, .185, 1}, rest = {.154, .124, .139, 1},
  ink = {.137, .095, .124, 1}, plum = {.208, .119, .151, 1},
  ember = {.489, .254, .201, 1}, peak = {.126, .087, .126, 1},
}

local function setColor(name, c)
  local index = ui.Col and ui.Col[name]
  if not index then return 0 end
  ui.PushStyleColor(index, c[1], c[2], c[3], c[4])
  return 1
end

local function fill(x, y, w, h, c, radius)
  ui.DrawRectFilled(x, y, x + w, y + h, c[1], c[2], c[3], c[4], radius or 0)
end

local function line(x, y, w, c)
  ui.DrawLine(x, y, x + w, y, c[1], c[2], c[3], c[4], 1)
end

local function drawText(x, y, value, c, size)
  ui.DrawText(x, y, c[1], c[2], c[3], c[4], value, size)
end

local function note(value, tone)
  local c = color[tone or "text"]
  ui.PushTextWrapPos(0)
  ui.TextColored(c[1], c[2], c[3], c[4], tostring(value or "Unavailable"))
  ui.PopTextWrapPos()
end

local function pushTheme(scale)
  local colors, vars = 0, 0
  local styles = {
    Text = color.text, TextDisabled = color.muted, ChildBg = color.card, Border = color.line,
    PopupBg = color.card, FrameBg = color.rest, FrameBgHovered = color.hover, FrameBgActive = color.active,
    Button = color.rest, ButtonHovered = color.hover, ButtonActive = color.active, Header = color.hover,
    HeaderHovered = color.hover, HeaderActive = color.active, NavCursor = color.gold,
  }
  for name, c in pairs(styles) do colors = colors + setColor(name, c) end
  for _, style in ipairs({
    {"WindowPadding", 13 * scale, 12 * scale}, {"FramePadding", 10 * scale, 5 * scale},
    {"ItemSpacing", 8 * scale, 6 * scale}, {"ChildRounding", 5 * scale}, {"FrameRounding", 4 * scale},
  }) do
    local index = ui.StyleVar and ui.StyleVar[style[1]]
    if index then
      if style[3] then ui.PushStyleVar(index, style[2], style[3]) else ui.PushStyleVar(index, style[2]) end
      vars = vars + 1
    end
  end
  return colors, vars
end

local function button(label, width, height, selected)
  local colors = 0
  if selected then
    colors = setColor("Button", color.active) + setColor("Text", color.bright)
  end
  local clicked = ui.Button(label, width or 0, height or 0)
  if colors > 0 then ui.PopStyleColor(colors) end
  return clicked
end

local function buttonWidth(label, minimum, scale)
  return math.max(minimum * scale, math.ceil(ui.CalcTextSize(label) + 20 * scale + 2))
end

local function heading(title)
  ui.PushFont(5)
  note(title, "gold")
  ui.PopFont()
end

local function wrapLines(title, width, ratio)
  local lines, current = {}, ""
  for word in title:gmatch("%S+") do
    local nextLine = current == "" and word or current .. " " .. word
    if current ~= "" and ui.CalcTextSize(nextLine) * ratio > width then
      lines[#lines + 1] = current
      current = word
    else
      current = nextLine
    end
  end
  lines[#lines + 1] = current
  return lines
end

local function featureHeading(title, scale)
  local width = ui.GetContentRegionAvail()
  local size = 23 * scale
  local _, baseHeight = ui.CalcTextSize("Ag")
  local lines = wrapLines(title, width, size / math.max(1, baseHeight))
  local x, y = ui.GetCursorScreenPos()
  drawText(x, y, table.concat(lines, "\n"), color.bright, size)
  ui.Dummy(0, #lines * size + 2 * scale)
end

local function guideButton(title, id, scale)
  local x, y = ui.GetCursorScreenPos()
  local width = ui.GetContentRegionAvail()
  local _, lineHeight = ui.CalcTextSize("Ag")
  local lines = wrapLines(title, width - 28 * scale, 1)
  local height = #lines * lineHeight + 8 * scale
  local clicked = ui.Selectable("##journal-guide:" .. id, false, 0, width, height)
  ui.PushClipRect(x, y, x + width, y + height, true)
  drawText(x + 5 * scale, y + 4 * scale, table.concat(lines, "\n"), color.gold)
  drawText(x + width - 14 * scale, y + 4 * scale, "›", color.gold)
  ui.PopClipRect()
  return clicked
end

local libraryStyle = {button = button, featureHeading = featureHeading, guideButton = guideButton,
  heading = heading, note = note}

local function rows(id, values)
  local flags = ui.TableFlags
  if not ui.BeginTable("##journal-fields-" .. id, 2, flags.BordersInnerH | flags.SizingStretchProp) then return end
  ui.TableSetupColumn("Field", 0, 1)
  ui.TableSetupColumn("Value", 0, 1)
  for _, entry in ipairs(values) do
    ui.TableNextRow()
    ui.TableNextColumn(); note(entry[1], "muted")
    ui.TableNextColumn(); note(entry[2], entry[3])
  end
  ui.EndTable()
end

local function card(id, width, height, draw)
  local flags = ui.WindowFlags and ui.WindowFlags.NoScrollbar or 0
  local colors = setColor("ChildBg", color.card)
  if ui.BeginChild("##journal-card-" .. id, width, height, true, flags) then draw() end
  ui.EndChild()
  if colors > 0 then ui.PopStyleColor(colors) end
end

local function groupCards(leftId, leftHeight, left, rightId, rightHeight, right, scale)
  local width = ui.GetContentRegionAvail()
  if width >= 575 * scale then
    local leftWidth = math.floor((width - 8 * scale) * .53)
    card(leftId, leftWidth, leftHeight, left)
    ui.SameLine()
    card(rightId, 0, rightHeight, right)
  else
    card(leftId, 0, leftHeight, left)
    card(rightId, 0, rightHeight, right)
  end
end

local function gradient(x, y, w, h, top, bottom)
  if ui.DrawRectGradient then
    ui.DrawRectGradient(x, y, x + w, y + h,
      top[1], top[2], top[3], top[4], bottom[1], bottom[2], bottom[3], bottom[4])
  else
    fill(x, y, w, h, top)
  end
end

local function tower(x, base, w, height, roof, c)
  fill(x, base - height, w, height, c)
  ui.DrawTriangleFilled(x, base - height, x + w * .5, base - height - roof, x + w, base - height,
    c[1], c[2], c[3], c[4])
end

local function skyline(x, y, w, h, scale)
  local center, middle = x + w * .785, y + h * .38
  local radius = math.min(58 * scale, w * .14)
  for layer = 7, 1, -1 do
    ui.DrawCircleFilled(center, middle, radius + (layer * 22 + 22) * scale,
      color.ember[1], color.ember[2], color.ember[3], .028)
  end
  ui.DrawCircleFilled(center, middle, radius + 21 * scale, .67, .32, .22, .08)
  ui.DrawCircleFilled(center, middle, radius + 11 * scale, .71, .32, .23, .12)
  ui.DrawCircle(center, middle, radius + 4 * scale, .91, .52, .38, .20, 7 * scale)
  ui.DrawCircleFilled(center, middle, radius, .133, .085, .116, 1)
  ui.DrawCircle(center, middle, radius, .94, .64, .43, .92, 2 * scale)
  ui.DrawCircle(center, middle, radius + 1 * scale, 1, .77, .54, .25, 4 * scale)

  local c = color.peak
  local base = y + h - 1
  local px = x + w * .48
  local rw = (x + w) - px
  fill(px, base - h * .095, rw, h * .095, c)
  ui.DrawTriangleFilled(px, base, px + rw * .20, base - h * .24, px + rw * .52, base, c[1], c[2], c[3], c[4])
  ui.DrawTriangleFilled(px + rw * .52, base, px + rw * .91, base - h * .31, px + rw, base, c[1], c[2], c[3], c[4])
  tower(px + rw * .17, base, rw * .115, h * .30, h * .095, c)
  tower(px + rw * .32, base, rw * .13, h * .43, h * .135, c)
  tower(px + rw * .48, base, rw * .09, h * .31, h * .055, c)
  tower(px + rw * .55, base, rw * .16, h * .55, h * .16, c)
  tower(px + rw * .75, base, rw * .12, h * .31, h * .084, c)
  tower(px + rw * .88, base, rw * .08, h * .22, h * .048, c)
end

local function hero(view, scale)
  local raid = view.mode == "normal" or view.mode == "hard"
  local copium = view.mode == "copium"
  local x, y = ui.GetCursorScreenPos()
  local w = ui.GetContentRegionAvail()
  local h = (copium and 116 or raid and 202 or 160) * scale
  local size = math.min(42 * scale, w / 11)
  ui.PushClipRect(x, y, x + w, y + h, true)
  gradient(x, y, w, h, color.plum, color.ink)
  if not copium and w >= 510 * scale then skyline(x, y, w, h, scale) end
  line(x, y + h - 1, w, color.active)
  local eyebrow = copium and "RELIC REDEMPTION" or raid and "CASTLE MISTMOORE / RAID" or "CASTLE MISTMOORE / GROUP"
  local title = copium and "Copium Tracker" or Guide[view.mode].title:gsub(" Last ", "\nLast "):gsub(" Midnight ", "\nMidnight ")
  local tagline = copium and "Check boss credit and compare relic rewards"
    or raid and "What can we run tonight?" or "The route to your first Normal raid"
  drawText(x + 20 * scale, y + 19 * scale, eyebrow, color.gold, 12 * scale)
  ui.PushFont(6)
  drawText(x + 20 * scale, y + 38 * scale, title, color.bright, size)
  ui.PopFont()
  drawText(x + 20 * scale, y + (copium and 86 or 125) * scale, tagline, color.muted, 14 * scale)
  ui.PopClipRect()

  if raid then
    ui.SetCursorScreenPos(x + 20 * scale, y + 152 * scale)
    if button("Normal##journal-mode", buttonWidth("Normal", 84, scale), 31 * scale, view.mode == "normal") then
      Model.navigate(view, "normal")
    end
    ui.SameLine()
    if button("Bloodbound##journal-mode", buttonWidth("Bloodbound", 106, scale), 31 * scale, view.mode == "hard") then
      Model.navigate(view, "hard")
    end
  end
  ui.SetCursorScreenPos(x, y + h - 1)
  ui.Dummy(w, 1)
end

local sections = {{"Overview", "overview"}, {"Encounters", "encounters"}, {"Roster", "roster"}}
local featuredSections = {sections[1], sections[3]}
local function tabs(view, snapshot, journal, now, scale, featured)
  local x, y = ui.GetCursorScreenPos()
  local width = ui.GetContentRegionAvail()
  fill(x, y, width, 35 * scale, color.chrome)
  local entries = featured and featuredSections or sections
  local spacing = (#entries - 1) * 8 * scale
  local tabWidth = buttonWidth("Encounters", 105, scale)
  local short = tabWidth * #entries + spacing > width
  if short then tabWidth = math.max(1, (width - spacing) / #entries) end
  for index, tab in ipairs(entries) do
    if index > 1 then ui.SameLine() end
    local label = short and tab[2] ~= "roster" and (tab[2] == "overview" and "Run" or "Guide") or tab[1]
    if button(label .. "##journal-tab", tabWidth, 34 * scale, view.tab == tab[2]) then
      view.tab = tab[2]
      if tab[2] == "roster" and Model.roster(snapshot, now).state == "stale" then Model.refresh(view, journal) end
    end
  end
end

local function relics(items, inventory)
  for _, relic in ipairs(items) do
    local count = Model.relicQuantity(inventory, relic.id)
    note(relic.name .. (type(count) ~= "number" and "" or count > 0 and " · Held" or " · Needed"),
      count and count > 0 and "good" or "muted")
  end
end

local function guideCard(view, info, expanded, scale)
  local vigil = view.mode == "vigil"
  local width = ui.GetContentRegionAvail()
  local columns = width >= 560 * scale and 2 or 1
  local intro = vigil and "Clear Mistmoore servants to recover four different relics for a first Normal raid request."
    or "Clear each district to call out its guardian. The four districts can be tackled in any order."
  local replay = vigil and "The replay timer begins when the group accepts the expedition."
    or "A fresh request goes on a 72-hour replay timer once the roster seals."

  -- Reserve the card's padding and each table cell's gutters before measuring wrapped rows.
  local innerWidth = width - 28 * scale
  local cellWidth = innerWidth / columns - 12 * scale
  local _, lineHeight = ui.CalcTextSize("Ag")
  local function textHeight(text, wrapWidth)
    if ui.CalcTextSize(text) <= wrapWidth then return lineHeight end
    return #wrapLines(text, math.max(1, wrapWidth), 1) * lineHeight
  end
  local height = math.max(80 * scale, textHeight(intro, innerWidth) + 60 * scale)
  for index = 1, #info.encounters, columns do
    local rowHeight = 73 * scale
    for item = index, math.min(index + columns - 1, #info.encounters) do
      local entry = info.encounters[item]
      rowHeight = math.max(rowHeight, textHeight(entry[1], cellWidth - 28 * scale)
        + textHeight(entry[2], cellWidth) + 36 * scale)
    end
    height = height + rowHeight
  end
  if expanded then height = height + math.max(48 * scale, textHeight(replay, innerWidth) + 10 * scale) end

  card("guide", 0, height, function()
    heading(expanded and not vigil and "Four district guardians" or "Quick encounter guide")
    note(intro, "muted")
    local flags = ui.TableFlags
    if ui.BeginTable("##journal-guardian-table", columns, flags.SizingStretchProp) then
      -- Full-width button and wrapped text measurements must not set the next frame's proportions.
      for column = 1, columns do ui.TableSetupColumn("##guardian-" .. column, 0, 1) end
      for index, entry in ipairs(info.encounters) do
        if (index - 1) % columns == 0 then ui.TableNextRow() end
        ui.TableNextColumn()
        if entry[3] and entry[3] ~= "" then
          if guideButton(entry[1], "expedition:" .. entry[3], scale) then Library.open(view, entry[3]) end
        else
          note(entry[1], "gold")
        end
        note(entry[2], "muted")
        ui.Dummy(0, 5 * scale)
      end
      ui.EndTable()
    end
    if expanded then note(replay, "muted") end
  end)
end

local function overview(view, snapshot, info, now, scale)
  local mode = view.mode
  local vigil = mode == "vigil"
  local unlock = snapshot.data and snapshot.data.unlock
  local width = ui.GetContentRegionAvail()
  local height = (width < 365 * scale and 250 or 221) * scale
  local runHeight = height + (view.entryOpen and 65 * scale or 0)
  local accessHeight = height + (not vigil and view.unlockOpen and (width < 510 * scale and 170 or 145) * scale or 0)
  groupCards("tonight", runHeight, function()
    local headline, detail = Model.headline(mode, snapshot, info, now)
    local saved, replay = Model.runLabels(snapshot, now)
    note("TONIGHT", "gold")
    featureHeading(headline, scale)
    note(detail, "muted")
    rows("run", {{"Saved run", saved}, {"Your " .. info.replayHours .. "h replay", replay}})
    if button("Entry instructions##journal-entry", 0, 26 * scale, view.entryOpen) then view.entryOpen = not view.entryOpen end
    if view.entryOpen then note(Model.entry(mode, snapshot, info, now), "muted") end
  end, "access", accessHeight, function()
    heading("Entry & access")
    note(Guide.entryName)
    note(Guide.entryLocation, "muted")
    local fields = {
      {vigil and "Group size" or "Raid size", info.minRoster .. "–" .. info.maxRoster .. " characters"},
      {vigil and "Minimum level" or "Difficulty", vigil and info.minLevel or info.tunedFor},
      {"Instance length", info.instanceHours .. " hours"},
    }
    if not vigil then fields[#fields + 1] = {"Your request unlock", Model.unlockLabel(unlock, info)} end
    rows("access", fields)
    if vigil then return end
    if button("Unlock requirements##journal-unlock", 0, 26 * scale, view.unlockOpen) then view.unlockOpen = not view.unlockOpen end
    if view.unlockOpen then
      note(mode == "hard" and "Redeem the writ to permanently unlock Bloodbound and TOFS raid requests for the requester."
        or "On the first request, the raid leader hands in all four relics together to unlock Normal permanently.", "muted")
      relics(info.relics, unlock)
    end
  end, scale)
  guideCard(view, info, false, scale)
  if vigil then
    card("admission", 0, 150 * scale, function()
      heading("Admission relics")
      relics(Guide.normal.relics, unlock)
    end)
  end
end

local function rosterCard(snapshot, now, scale, supplied, featured)
  local roster = Model.roster(snapshot, now, supplied)
  local members = roster.members or {}
  local savedRun = roster.kind == "run"
  local passed, blocked = 0, 0
  for _, member in ipairs(members) do
    if member.state == "eligible" then passed = passed + 1
    elseif member.state == "blocked" then blocked = blocked + 1 end
  end
  local width = ui.GetContentRegionAvail()
  card("roster", 0, (104 + #members * (width < 480 * scale and 41 or 31)) * scale, function()
    local label = "Roster eligibility"
    if #members > 0 then
      label = savedRun and (#members .. " assigned · Saved run")
        or (passed .. " of " .. #members .. " pass" .. (blocked > 0 and " · " .. blocked .. " blocked" or ""))
    end
    featureHeading(label, scale)
    note(Model.rosterReason(roster, snapshot, featured), "muted")
    if #members == 0 then return end
    local flags = ui.TableFlags
    if ui.BeginTable("##journal-roster-table", 2, flags.BordersInnerH | flags.SizingStretchProp) then
      ui.TableSetupColumn("Character", 0, 1)
      ui.TableSetupColumn("Server status", 0, 1.5)
      ui.TableHeadersRow()
      for _, member in ipairs(members) do
        ui.TableNextRow()
        ui.TableNextColumn(); note((member.name or "Character") .. (member.isLeader and " · Leader" or ""))
        ui.TableNextColumn()
        local status, tone = Model.memberStatus(member, savedRun, snapshot, now)
        note(status, tone)
      end
      ui.EndTable()
    end
  end)
end

local creditText = {credited = "Credited", uncredited = "Credit required", unavailable = "Unavailable"}
local rewardText = {
  available = "Available", lore_conflict = "Lore conflict", unavailable = "Availability unknown",
  credit_required = "Boss credit required", relic_required = "Relic required",
}

local function copiumCard(view, list, detail, scale)
  local options = list.data and list.data.relics
  if type(options) ~= "table" or #options == 0 then
    card("copium-offline", 0, 101 * scale, function()
      heading(list.pending and "Checking Copium credit" or "Redemption status unavailable")
      note(list.pending and "Reading relic inventory and boss credit." or "Master Artificer Copium can check your relic inventory and boss credit.", "muted")
    end)
    return
  end

  local chosen
  for _, relic in ipairs(options) do
    if relic.relicId == view.relicId then chosen = relic; break end
  end
  card("copium-picker", 0, 80 * scale, function()
    heading("Choose a relic")
    ui.SetNextItemWidth(math.min(ui.GetContentRegionAvail(), 400 * scale))
    if ui.BeginCombo("##journal-relic-picker", chosen and chosen.name or "Select relic") then
      for _, relic in ipairs(options) do
        local label = (relic.name or "Relic") .. ((relic.quantity or 0) > 0 and " · " .. relic.quantity .. " held" or "")
        if ui.Selectable(label .. "##journal-relic-" .. relic.relicId, relic.relicId == view.relicId) then
          view.relicId = relic.relicId
        end
      end
      ui.EndCombo()
    end
  end)
  if not chosen then return end
  local relic = detail.data and detail.data.relic or chosen
  if relic.relicId ~= chosen.relicId then relic = chosen end
  local height = 184 * scale
  groupCards("copium-status", height, function()
    note(relic.name, "gold")
    local label = "Redemption status unavailable"
    if relic.credit == "uncredited" then label = "Boss credit required"
    elseif relic.credit == "credited" and type(relic.quantity) == "number" then
      label = relic.quantity > 0 and "Ready to visit Copium" or "Relic needed"
    end
    heading(label)
    note("Master Artificer Copium · Plane of Knowledge", "muted")
    rows("copium-status", {
      {"Inventory", type(relic.quantity) == "number" and (relic.quantity .. " held") or "Unavailable"},
      {"Boss credit", creditText[relic.credit] or "Unavailable", relic.credit == "credited" and "good" or "muted"},
    })
  end, "copium-credit", height, function()
    heading("Exact credit")
    note(relic.relicId == 170009 and "Successfully rescue Jelvan in Anguish."
      or "Recorded credit for " .. (relic.name or "this encounter") .. ".", "muted")
    rows("copium-credit", {
      {"Relic", relic.relicId}, {"Achievement", relic.achievementId},
      {"Component", relic.componentId}, {"Component type", relic.componentType},
    })
  end, scale)
  local rewards = relic.rewards or {}
  local columns = ui.GetContentRegionAvail() >= 560 * scale and 2 or 1
  card("copium-rewards", 0, (105 + math.ceil(#rewards / columns) * 52) * scale, function()
    heading(#rewards > 0 and (#rewards .. " relic reward choices") or "Relic reward choices")
    if #rewards == 0 then
      note(detail.pending and "Checking reward choices." or "Reward choices are unavailable.", "muted")
      return
    end
    note("Copium checks credit, inventory and lore conflicts when you claim a reward.", "muted")
    local flags = ui.TableFlags
    if ui.BeginTable("##journal-reward-table", columns, flags.BordersInnerH | flags.SizingStretchProp) then
      for index, reward in ipairs(rewards) do
        if (index - 1) % columns == 0 then ui.TableNextRow() end
        ui.TableNextColumn()
        note((reward.name or "Item") .. (reward.itemId and " · " .. reward.itemId or ""))
        note(rewardText[reward.availability] or "Availability unknown",
          reward.availability == "available" and "good" or reward.availability == "lore_conflict" and "danger" or "muted")
      end
      ui.EndTable()
    end
  end)
end

local function sleeperRows(snapshot, now)
  local data = snapshot.data or {}
  local unknown = snapshot.pending and "Checking…" or "Unavailable"
  local function phase(value)
    if value == "1.0" or value == "2.0" then return value end
    return unknown
  end
  local rotation = Model.remaining(snapshot, data.nextRotationAt, now)
  local nextLabel = data.rotation == "guildless" and "Your next Static Mode request" or "Your guild’s next Static Mode request"
  local nextPhase = rotation and rotation <= 0 and "Refresh to check the new week" or phase(data.nextPhase)
  local values = {{"Public Tomb", phase(data.publicPhase)}, {nextLabel, nextPhase}}
  if data.rotation == "alternating" and rotation and rotation > 0 then
    values[#values + 1] = {"Next guild rotation", Model.duration(rotation)}
  elseif data.rotation == "locked" then
    values[#values + 1] = {"Guild rotation", "Unlock by defeating two distinct original Warders"}
  end
  local saved = data.savedRun
  if type(saved) == "table" and (saved.phase == "1.0" or saved.phase == "2.0") then
    local remaining = Model.remaining(snapshot, saved.expiresAt, now)
    if not remaining or remaining > 0 then values[#values + 1] = {"Your saved run", saved.phase} end
  end
  return values
end

local function featuredStatus(view, info, snapshot, now, scale)
  local data = snapshot.data or {}
  local title, detail = Model.featuredHeadline(snapshot, now)
  heading("Tonight")
  featureHeading(title, scale)
  note(detail, "muted")
  if not snapshot.data then return end

  local saved, replay = Model.runLabels(snapshot, now)
  local access, tone = Model.featuredUnlock(data.unlock)
  local values = {{"Saved run", saved}, {"Replay lockout", replay}, {"Your entry access", access, tone}}
  if data.run and data.run.chapter then
    local remaining = Model.remaining(snapshot, data.run.expiresAt, now)
    if not remaining or remaining > 0 then values[#values + 1] = {"Saved chapter", data.run.chapter} end
  end
  if info.id == "firefell" then
    values[#values + 1] = {"Completion lockout", Model.lockoutLabel(snapshot, data.completion, now)}
  end
  for _, item in ipairs(data.unlock and data.unlock.items or {}) do
    if type(item.quantity) == "number" and type(item.required) == "number" then
      values[#values + 1] = {item.name, item.quantity .. " of " .. item.required .. " held"}
    end
  end
  if data.unlock and data.unlock.payment == "paid" then
    values[#values + 1] = {"Raid offering", "Already paid. Ask Avel for raid to retry."}
  elseif data.unlock and data.unlock.payment == "pending" then
    values[#values + 1] = {"Raid offering", "Pending. Ask Avel to confirm before giving more Splinters."}
  end
  rows("featured-status-" .. view.mode, values)

  if data.chapters and #data.chapters > 0 then
    ui.Separator()
    heading("Chapter access")
    local chapters = {}
    for _, chapter in ipairs(data.chapters) do
      local label, chapterTone = Model.featuredUnlock(chapter)
      chapters[#chapters + 1] = {chapter.name, label, chapterTone}
    end
    rows("featured-chapters", chapters)
  end
end

local function chapterRoster(view, snapshot, scale)
  local data = snapshot.data
  if not data or data.roster and data.roster.kind == "run" or not data.chapters then return end
  local choices, selected = {}, nil
  for _, chapter in ipairs(data.chapters) do
    if chapter.roster then
      choices[#choices + 1] = chapter
      if chapter.name == (view.featuredChapter or data.roster and data.roster.chapter) then selected = chapter end
    end
  end
  selected = selected or choices[1]
  if not selected then return end
  heading("Firefell chapter")
  ui.SetNextItemWidth(math.min(ui.GetContentRegionAvail(), 400 * scale))
  if ui.BeginCombo("##journal-roster-chapter", selected.name) then
    for _, chapter in ipairs(choices) do
      if ui.Selectable(chapter.name, selected == chapter) then
        selected = chapter
        view.featuredChapter = chapter.name
      end
    end
    ui.EndCombo()
  end
  return data.roster and data.roster.chapter == selected.name and data.roster or selected.roster
end

local function featuredPage(view, info, snapshot, now, scale, journal)
  heading(info.zone .. " / " .. info.subtitle)
  featureHeading(info.title, scale)
  note(info.description, "muted")
  if info.id == "sleepers" then
    ui.Separator()
    heading("Current phase")
    rows("sleeper-phases", sleeperRows(snapshot, now))
  else
    tabs(view, snapshot, journal, now, scale, true)
    if view.tab == "roster" then
      rosterCard(snapshot, now, scale, info.id == "firefell" and chapterRoster(view, snapshot, scale) or nil, true)
      return
    end
    featuredStatus(view, info, snapshot, now, scale)
  end
  if #info.access > 0 then
    ui.Separator()
    heading("Entry & access")
    rows("featured-access", info.access)
  end
  if #info.encounters > 0 then
    ui.Separator()
    heading("Encounter guides")
    Library.links(view, info.encounters, scale, libraryStyle)
  end
end

local function header(view, window, journal, snapshot, detail, scale)
  local x, y = ui.GetCursorScreenPos()
  local width = ui.GetContentRegionAvail()
  local height = 36 * scale
  local staticPage = not Model.liveMode(view.mode) or view.encounter ~= nil
  local refreshWidth = staticPage and 0 or buttonWidth("Refresh", 72, scale)
  local refreshX = x + width - refreshWidth - 8 * scale
  fill(x, y, width, height, color.chrome)
  line(x, y + height - 1, width, color.line)
  drawText(x + 12 * scale, y + 11 * scale, "Expedition Journal", color.text, 14 * scale)
  local status = staticPage and "Encounter guides"
    or snapshot.data and (window.character or snapshot.data.character or "Live character")
    or snapshot.pending and "Checking character" or "Live status unavailable"
  if width >= 460 * scale then
    local statusWidth = ui.CalcTextSize(status) + 20 * scale
    local sx = refreshX - statusWidth
    ui.DrawCircleFilled(sx, y + 18 * scale, 3 * scale, color.gold[1], color.gold[2], color.gold[3], .9)
    ui.PushClipRect(sx + 8 * scale, y, refreshX - 8 * scale, y + height, true)
    drawText(sx + 9 * scale, y + 12 * scale, status, color.muted, 12 * scale)
    ui.PopClipRect()
  end
  if not staticPage then
    ui.SetCursorScreenPos(refreshX, y + 5 * scale)
    ui.BeginDisabled(not window.connected or snapshot.pending == true or detail.pending == true)
    if button("Refresh##journal-status", refreshWidth, 26 * scale) then Model.refresh(view, journal) end
    ui.EndDisabled()
  end
  ui.SetCursorScreenPos(x, y + height - 1)
  ui.Dummy(width, 1)
end

local routes = {
  {"Encounter Library", "library", "Library", 4, "GUIDES"},
  {"Mistmoore Eclipse", "raidMode", "Eclipse", 1, "CASTLE MISTMOORE"},
  {"Midnight Vigil", "vigil", "Vigil", 2},
  {"Copium tracker", "copium", "Copium", 3, "REWARDS"},
}

local function navigation(view, vertical, scale)
  local width = ui.GetContentRegionAvail()
  local x, y = ui.GetCursorScreenPos()
  if vertical then
    local _, height = ui.GetContentRegionAvail()
    fill(x, y, width, height, color.rail)
  end
  local minimum = buttonWidth("Library", 0, scale)
  local columns = width >= 4 * minimum + 24 * scale and 4 or 2
  local unit = vertical and width or math.max(1, (width - (columns - 1) * 8 * scale) / columns)
  for index, route in ipairs(routes) do
    if not vertical and (index - 1) % columns ~= 0 then ui.SameLine() end
    if vertical and route[5] then
      if index > 1 then ui.Dummy(0, 14 * scale) end
      note(route[5], "muted")
    end
    local title = route[1]
    if not vertical and unit < buttonWidth(title, 0, scale) then
      title = route[3]
    end
    local mode = route[2] == "raidMode" and view.raidMode or route[2]
    if button(title .. "##journal-route-" .. route[4], unit, 32 * scale, mode == view.mode) then
      Model.navigate(view, mode, true)
    end
  end
  if vertical then
    ui.Dummy(0, 14 * scale)
    note("MORE EXPEDITIONS", "muted")
    for _, entry in ipairs(Featured) do
      if button(entry.label .. "##journal-featured-" .. entry.id, unit, 32 * scale, view.mode == entry.id) then
        Model.navigate(view, entry.id, true)
      end
    end
  else
    local current = featuredById[view.mode]
    ui.SetNextItemWidth(-1)
    if ui.BeginCombo("##journal-featured", current and current.label or "More expeditions") then
      for _, entry in ipairs(Featured) do
        if ui.Selectable(entry.label .. "##journal-featured-" .. entry.id, view.mode == entry.id) then
          Model.navigate(view, entry.id, true)
        end
      end
      ui.EndCombo()
    end
  end
end

function M.draw(view, window, journal, scale)
  local now = ui.GetTime()
  -- The gamepad pack can select a larger font at the same viewport scale.
  local _, lineHeight = ui.CalcTextSize("Ag")
  scale = math.max(scale, lineHeight / 15)
  local colors, vars = pushTheme(scale)
  local x, y = ui.GetCursorScreenPos()
  local width, height = ui.GetContentRegionAvail()
  fill(x, y, width, height, color.bg)
  local snapshot, detail = Model.poll(view, window, journal, now)
  header(view, window, journal, snapshot, detail, scale)
  local railWidth = math.max(buttonWidth("Mistmoore Eclipse", 156, scale),
    buttonWidth("Encounter Library", 156, scale)) + 26 * scale
  for _, entry in ipairs(Featured) do railWidth = math.max(railWidth, buttonWidth(entry.label, 0, scale) + 26 * scale) end
  local vertical = width >= math.max(770 * scale, railWidth + 470 * scale)
  local modeBefore = view.mode
  if vertical then
    local railColor = setColor("ChildBg", color.rail)
    if ui.BeginChild("##journal-navigation", railWidth, 0, true) then navigation(view, true, scale) end
    ui.EndChild()
    if railColor > 0 then ui.PopStyleColor(railColor) end
    ui.SameLine()
  else
    navigation(view, false, scale)
  end
  if modeBefore ~= view.mode then snapshot, detail = Model.poll(view, window, journal, now) end
  local bodyColor = setColor("ChildBg", color.bg)
  if ui.BeginChild("##journal-page", 0, 0, true) then
    if view.scroll then
      ui.SetScrollY(type(view.scroll) == "number" and view.scroll or 0)
      view.scroll = false
    end
    view.pageScrollY = ui.GetScrollY()
    if view.encounter then
      Library.detail(view, scale, libraryStyle)
    elseif view.mode == "library" then
      Library.list(view, scale, libraryStyle)
    elseif featuredById[view.mode] then
      featuredPage(view, featuredById[view.mode], snapshot, now, scale, journal)
    else
      modeBefore = view.mode
      hero(view, scale)
      if modeBefore ~= view.mode then snapshot, detail = Model.poll(view, window, journal, now) end
      if view.mode == "copium" then
        copiumCard(view, snapshot, detail, scale)
      else
        local info = Guide[view.mode]
        tabs(view, snapshot, journal, now, scale)
        if view.tab == "roster" then rosterCard(snapshot, now, scale)
        elseif view.tab == "encounters" then guideCard(view, info, true, scale)
        else overview(view, snapshot, info, now, scale) end
      end
    end
  end
  ui.EndChild()
  if bodyColor > 0 then ui.PopStyleColor(bodyColor) end
  if vars > 0 then ui.PopStyleVar(vars) end
  if colors > 0 then ui.PopStyleColor(colors) end
end

M.new, M.reset = Model.new, Model.reset
return M
