-- SPDX-License-Identifier: MIT
local Host = dofile("lib/bastion_host.lua")
local Planner = {}

local colors = {
  slate = {0.086, 0.114, 0.149, 1}, raised = {0.125, 0.165, 0.216, 1},
  side = {0.094, 0.129, 0.169, 1}, line = {0.208, 0.263, 0.325, 1},
  text = {0.94, 0.925, 0.875, 1}, muted = {0.67, 0.72, 0.78, 1},
  gold = {0.91, 0.76, 0.47, 1}, bronze = {0.27, 0.22, 0.15, 1},
  green = {0.57, 0.84, 0.70, 1}, warning = {1, 0.69, 0.36, 1},
  hover = {0.23, 0.29, 0.37, 1},
}
local skills = {"Alchemy", "Baking", "Blacksmithing", "Brewing", "Fletching", "Jewelcrafting",
  "Poison Making", "Pottery", "Research", "Tailoring", "Tinkering"}
local tabs = {"Shopping", "Combines", "Recipe choices"}
local mainTitle = "Tradeskill Plans###bastion-tradeskill-plans"
local browserTitle = "Browse recipes###bastion-recipe-browser"
local trackerTitle = "Tradeskill Tracker###bastion-tradeskill-tracker"

local function note(value, color)
  if value and value ~= "" then
    local ink = color or colors.text
    ui.PushTextWrapPos(0)
    ui.TextColored(ink[1], ink[2], ink[3], ink[4], value)
    ui.PopTextWrapPos()
  end
end

local function heading(value)
  ui.PushFont(6)
  note(value)
  ui.PopFont()
end

local function details(skill, trivial, station)
  local parts = {}
  if skill and skill ~= "" then parts[#parts + 1] = skill end
  if trivial and trivial > 0 then parts[#parts + 1] = "Trivial " .. trivial end
  if station and station ~= "" then parts[#parts + 1] = station end
  return table.concat(parts, "  /  ")
end

local function styles(scale)
  ui.PushStyleColor(ui.Col.WindowBg, table.unpack(colors.slate))
  ui.PushStyleColor(ui.Col.ChildBg, table.unpack(colors.side))
  ui.PushStyleColor(ui.Col.Border, table.unpack(colors.line))
  ui.PushStyleColor(ui.Col.Text, table.unpack(colors.text))
  ui.PushStyleColor(ui.Col.TextDisabled, table.unpack(colors.muted))
  ui.PushStyleColor(ui.Col.TitleBg, table.unpack(colors.raised))
  ui.PushStyleColor(ui.Col.TitleBgActive, table.unpack(colors.raised))
  ui.PushStyleColor(ui.Col.CheckMark, table.unpack(colors.gold))
  ui.PushStyleColor(ui.Col.FrameBg, table.unpack(colors.raised))
  ui.PushStyleColor(ui.Col.Button, table.unpack(colors.raised))
  ui.PushStyleColor(ui.Col.ButtonHovered, table.unpack(colors.hover))
  ui.PushStyleVar(ui.StyleVar.WindowRounding, 8 * scale)
  ui.PushStyleVar(ui.StyleVar.ChildRounding, 6 * scale)
  ui.PushStyleVar(ui.StyleVar.FrameRounding, 5 * scale)
  ui.PushStyleVar(ui.StyleVar.WindowPadding, 14 * scale, 12 * scale)
  ui.PushStyleVar(ui.StyleVar.ItemSpacing, 8 * scale, 7 * scale)
  ui.PushStyleVar(ui.StyleVar.FramePadding, 7 * scale, 4 * scale)
end

local function unstyle()
  ui.PopStyleVar(6)
  ui.PopStyleColor(11)
end

local function rule()
  local x, y = ui.GetCursorScreenPos()
  local width = ui.GetContentRegionAvail()
  ui.DrawLine(x, y + 2, x + width, y + 2, colors.line[1], colors.line[2], colors.line[3], 1, 1)
  ui.Dummy(0, 2)
end

local function button(label, width, height, primary, enabled)
  if primary then
    ui.PushStyleColor(ui.Col.Button, table.unpack(colors.bronze))
    ui.PushStyleColor(ui.Col.Text, table.unpack(colors.gold))
  end
  ui.BeginDisabled(enabled == false)
  local pressed = ui.Button(label, width or 0, height or 0)
  ui.EndDisabled()
  if primary then ui.PopStyleColor(2) end
  return pressed and enabled ~= false
end

local function child(id, width, height, draw, flags)
  local visible = ui.BeginChild(id, width, height, true, flags or 0)
  if visible then draw() end
  ui.EndChild()
end

local function openWindow(title, width, height, scale, focus)
  local io = ui.GetIO()
  local maximumW = math.max(1, io.width - 24)
  local maximumH = math.max(1, io.height - 24)
  local w, h = math.min(maximumW, width * scale), math.min(maximumH, height * scale)
  ui.SetNextWindowSize(w, h, ui.Cond.FirstUseEver)
  ui.SetNextWindowPos((io.width - w) / 2, (io.height - h) / 2, ui.Cond.FirstUseEver)
  ui.SetNextWindowSizeConstraints(math.min(360 * scale, maximumW), math.min(260 * scale, maximumH), maximumW, maximumH)
  if focus then
    ui.SetNextWindowFocus()
    -- A saved collapsed state should not hide a newly opened tool.
    ui.SetNextWindowCollapsed(false, ui.Cond.Always)
  end
  return ui.Begin(title, true, ui.WindowFlags.NoCollapse)
end

local function ready(state)
  return state.available == true and not state.pending
end

local function accepted(action, ...)
  local response = action(...)
  return response == true or (type(response) == "table" and response.accepted == true)
end

local function unpin(action, view)
  if accepted(action, "unpin") then view.pin, view.pinnedPlan = nil, nil end
end

local function editedNumber(label, value, available, maximum)
  ui.BeginDisabled(not available)
  local text = ui.InputText(label, value, 10, ui.InputTextFlags.CharsDecimal)
  ui.EndDisabled()
  local number = text:match("^%d+$") and tonumber(text)
  return text, number and number <= maximum and number or nil
end

local function groupRows(rows, key)
  local groups, byName = {}, {}
  for _, row in ipairs(rows or {}) do
    local name = row[key]
    if not name or name == "" then name = "Other" end
    if not byName[name] then
      byName[name] = {}
      groups[#groups + 1] = name
    end
    local group = byName[name]
    group[#group + 1] = row
  end
  return groups, byName
end

local function editable(state)
  return ready(state) and state.fresh == true and not state.queued
end

local function change(action, state, plan, kind, key, value)
  if editable(state) then
    action("update", plan.id, tostring(plan.revision), kind, key, tostring(value))
  end
end

local inventoryModule, inventoryCharacter, inventoryRevision, bagCounts, bankCounts, sharedCounts, bankLive
local function clearInventory()
  inventoryModule, inventoryCharacter, inventoryRevision = nil, nil, nil
  bagCounts, bankCounts, sharedCounts, bankLive = nil, nil, nil, nil
end

local function inventory()
  local api = hs and hs.UI and hs.UI.inventory
  if not api or not api.AllItems then return end
  local character = hs.UI.character and hs.UI.character.Name and hs.UI.character.Name()
  local revision = api.ItemsRevision and api.ItemsRevision()
  if inventoryModule == api and character == inventoryCharacter and revision ~= nil and revision == inventoryRevision then
    return bagCounts, bankCounts, sharedCounts, bankLive
  end
  local all = api.AllItems()
  if not all then return end
  local areas = {carried = {}, bank = {}, shared = {}}
  for _, row in ipairs(all) do
    local index = areas[row.where]
    local item = row.item
    if index and item and item.id then
      index[item.id] = (index[item.id] or 0) + (item.stackable and math.max(1, item.count or 0) or 1)
    end
  end
  inventoryModule, inventoryCharacter, inventoryRevision = api, character, revision
  bagCounts, bankCounts, sharedCounts = areas.carried, areas.bank, areas.shared
  bankLive = api.BankSeen and api.BankSeen()
  return bagCounts, bankCounts, sharedCounts, bankLive
end

local function coveredCount(row, bags)
  local item = tonumber(row.item_id)
  local count = bags and item and item > 0 and (bags[item] or 0) or nil
  if count == nil then return end
  return count, math.max(0, row.quantity - count)
end

local function progress(plan, scale)
  local done, total = plan.completed or 0, plan.total or 0
  note("Checklist", colors.muted)
  ui.SameLine()
  local label = done .. " / " .. total
  local barWidth = ui.GetContentRegionAvail() - ui.CalcTextSize(label) - 12 * scale
  if barWidth > 28 * scale then
    local x, y = ui.GetCursorScreenPos()
    local _, textHeight = ui.CalcTextSize(label)
    local barTop = y + math.floor(textHeight / 2)
    local r = colors.line
    ui.DrawRectFilled(x, barTop, x + barWidth, barTop + 5 * scale, r[1], r[2], r[3], 1, 3 * scale)
    if total > 0 and done > 0 then
      local g = colors.gold
      ui.DrawRectFilled(x, barTop, x + barWidth * math.min(done / total, 1), barTop + 5 * scale, g[1], g[2], g[3], 1, 3 * scale)
    end
    ui.Dummy(barWidth, textHeight)
    ui.SameLine()
  end
  note(label)
end

local function markStep(action, state, plan, row, label)
  ui.BeginDisabled(not editable(state) or row.checkable == false)
  local value, changed = ui.Checkbox(label .. "##progress", row.completed == true or row.checkable == false)
  ui.EndDisabled()
  if ui.IsItemHovered() or ui.IsItemFocused() then ui.SetTooltip("Mark " .. row.name) end
  if changed then change(action, state, plan, "step", row.id, value and 1 or 0) end
end

local function drawAmount(row, count, missing)
  if count ~= nil then
    note(missing == 0 and "Covered" or missing .. " to get", missing == 0 and colors.green or colors.gold)
    note(count .. " / " .. row.quantity .. " in bags", colors.muted)
  else
    note(row.quantity .. "×", colors.muted)
  end
end

local function drawStep(action, state, plan, row, listKind, scale, counts, editing)
  ui.PushID(tostring(row.id))
  local canEdit = editable(state)
  local label = row.name
  local count, missing = coveredCount(row, counts and counts.bags)
  local subtitle = listKind == "material" and row.source or details(row.skill, row.trivial, row.station)
  local width = ui.GetContentRegionAvail()
  local wide = width >= 365 * scale
  local item = tonumber(row.item_id)
  local bank = counts and item and counts.bank[item] or 0
  local shared = counts and item and counts.shared[item] or 0
  local hasSourcing = bank + shared > 0
  local _, textHeight = ui.CalcTextSize(label)
  local height = math.max(49 * scale, 2 * textHeight + 16 * scale)
  if not wide then height = height + 2 * textHeight + 16 * scale end
  if hasSourcing then height = height + textHeight + 8 * scale end
  ui.PushStyleVar(ui.StyleVar.WindowPadding, 8 * scale, 4 * scale)
  ui.PushStyleVar(ui.StyleVar.ItemSpacing, 5 * scale, 3 * scale)
  child("##step", 0, height, function()
    if wide and ui.BeginTable("##step-columns", 3, ui.TableFlags.SizingStretchProp) then
      ui.TableSetupColumn("##source", ui.TableColumnFlags.WidthStretch, 1)
      ui.TableSetupColumn("##amount", ui.TableColumnFlags.WidthFixed, 126 * scale)
      ui.TableSetupColumn("##mark", ui.TableColumnFlags.WidthFixed, 32 * scale)
      ui.TableNextRow()
      ui.TableNextColumn(); note(label, row.completed and colors.green or colors.text); note(subtitle, colors.muted)
      ui.TableNextColumn(); drawAmount(row, count, missing)
      ui.TableNextColumn(); markStep(action, state, plan, row, "")
      ui.EndTable()
    else
      markStep(action, state, plan, row, label:gsub("#", ""))
      note(subtitle, colors.muted)
      drawAmount(row, count, missing)
    end
    if hasSourcing then
      local hints = {}
      if bank > 0 then hints[#hints + 1] = bank .. (counts.bankLive and " in bank" or " last seen in bank") end
      if shared > 0 then hints[#hints + 1] = shared .. (counts.bankLive and " in shared bank" or " last seen in shared bank") end
      note(table.concat(hints, "  /  "), colors.muted)
    end
  end, ui.WindowFlags.NoScrollbar | ui.WindowFlags.NoScrollWithMouse)
  ui.PopStyleVar(2)
  if listKind == "material" and editing then
    local current = tostring(row.owned or 0)
    local field = editing[row.id]
    if not field then field = {value = current, reported = current}; editing[row.id] = field end
    if field.value == field.reported then field.value = current end
    field.reported = current
    ui.SetNextItemWidth(96 * scale)
    local value, quantity = editedNumber("Owned##material", field.value, canEdit, 1000000)
    field.value = value
    ui.SameLine()
    if button("Save##material", nil, nil, true, canEdit and quantity ~= nil and quantity ~= (row.owned or 0)) then
      change(action, state, plan, "inventory", row.item_key or row.id, quantity)
    end
  end
  ui.PopID()
end

local function visibleStatus(state)
  if state.message and state.message ~= "" then
    note(state.message, state.fresh and colors.muted or colors.warning)
  elseif state.pending then
    note("Syncing with Bastion…", colors.muted)
  elseif not state.available then
    note("Tradeskill plans are unavailable on this server.", colors.warning)
  elseif not state.fresh then
    note("Refresh to reconnect to your saved plans.", colors.warning)
  else
    note("Account plan", colors.green)
    note("Shared with the website", colors.muted)
  end
end

local function itemButton(label, id, scale, selected, enabled)
  ui.PushID(tostring(id))
  local pressed = button(label:gsub("#", "") .. "##pick", ui.GetContentRegionAvail(), 29 * scale, selected, enabled)
  if ui.IsItemHovered() then ui.SetTooltip(label) end
  ui.PopID()
  return pressed
end

local function openPlan(action, view, id)
  if accepted(action, "open", id) then
    view.requestedPlan = id
    view.import = false
    view.archive = false
  end
end

local function searchPage(action, view, page)
  if accepted(action, "search", view.skill, view.query, tostring(page)) then
    view.page = page
    view.recipe = nil
    view.searched = true
    view.lastQuery, view.lastSkill = view.query, view.skill
  end
end

local function drawSidebar(action, state, view, scale)
  local available = ui.GetContentRegionAvail()
  if button("Browse recipes", available, 30 * scale, true, state.available) then
    view.browser, view.focusBrowser = true, true
    if not view.searched and ready(state) then searchPage(action, view, 1) end
  end
  note("Saved plans")
  for _, plan in ipairs(state.plans or {}) do
    local selected = state.plan and state.plan.id == plan.id
    if itemButton(plan.name, plan.id, scale, selected, ready(state)) and not selected then
      openPlan(action, view, plan.id)
    end
    note(plan.quantity .. " target  /  " .. (plan.completed or 0) .. " of " .. (plan.total or 0) .. " steps", colors.muted)
  end
  if #(state.plans or {}) == 0 and state.fresh then note("No saved plans yet.", colors.muted) end
  rule()
  if button("Import browser saves", available, 28 * scale, false, true) then view.import = not view.import end
  if button("Refresh", available, 28 * scale, false, ready(state) and not state.queued) then
    view.requestedPlan = nil
    action("refresh")
  end
  visibleStatus(state)
end

local function drawQueued(action, state, view, scale)
  if not state.queued then
    view.discardPending = false
    return
  end
  note("An edit is saved on this computer and waiting to sync.", colors.warning)
  if view.discardPending then
    note("Discard the local pending edit? Your last account save will be kept.", colors.warning)
    if button("Discard local edit", nil, 27 * scale, true, not state.pending) then
      action("discard_pending")
      view.discardPending = false
    end
    ui.SameLine()
    if button("Cancel discard", nil, 27 * scale, false, true) then view.discardPending = false end
  else
    if button("Retry sync", nil, 27 * scale, true, not state.pending) then action("retry") end
    ui.SameLine()
    if button("Review pending edit", nil, 27 * scale, false, not state.pending) then view.discardPending = true end
  end
end

local function drawTabs(view, scale)
  local width = ui.GetContentRegionAvail()
  local nextWidth = width
  for index, label in ipairs(tabs) do
    local buttonWidth = ui.CalcTextSize(label) + 18 * scale
    if nextWidth < width and nextWidth > buttonWidth then
      ui.SameLine()
    elseif nextWidth < buttonWidth then
      nextWidth = width
    end
    if button(label .. "##tradeskill-view", math.min(buttonWidth, width), 27 * scale, view.tab == index, true) then
      view.tab = index
    end
    nextWidth = nextWidth - buttonWidth - 9 * scale
  end
  rule()
end

local function drawGroupChoices(names, selected, scale)
  if #names < 2 then return names[1] end
  local chosen = selected or names[1]
  if #names <= 4 then
    local remaining = ui.GetContentRegionAvail()
    local initial = remaining
    for index, label in ipairs(names) do
      local width = math.min(initial, ui.CalcTextSize(label) + 18 * scale)
      if index > 1 then
        if remaining >= width then ui.SameLine() else remaining = initial end
      end
      if button(label:gsub("#", "") .. "##source-group", width, 26 * scale, label == chosen, true) then chosen = label end
      remaining = remaining - width - 9 * scale
    end
  else
    ui.SetNextItemWidth(math.min(300 * scale, ui.GetContentRegionAvail()))
    if ui.BeginCombo("##source-group", chosen) then
      for index, label in ipairs(names) do
        if ui.Selectable(label:gsub("#", "") .. "##source" .. index, label == chosen) then chosen = label end
      end
      ui.EndCombo()
    end
  end
  return chosen
end

local function drawSteps(action, state, view, plan, scale)
  local isShopping = view.tab == 1
  local rows = isShopping and plan.materials or plan.combines
  if isShopping then
    note("Ingredients by source")
    local bagsLabel = view.bags and "Hide bag counts" or "Check bags"
    local ownedLabel = view.owned and "Hide owned quantities" or "Edit owned quantities"
    if ui.GetContentRegionAvail() >= ui.CalcTextSize("Ingredients by source") + ui.CalcTextSize(bagsLabel)
        + ui.CalcTextSize(ownedLabel) + 42 * scale then ui.SameLine() end
    if button(bagsLabel, nil, 27 * scale, false, true) then view.bags = not view.bags end
    if ui.GetContentRegionAvail() >= ui.CalcTextSize(bagsLabel) + ui.CalcTextSize(ownedLabel) + 28 * scale then ui.SameLine() end
    if button(ownedLabel, nil, 27 * scale, false, true) then
      view.owned = not view.owned
    end
  else
    note("Combines by station")
  end
  if not rows or #rows == 0 then
    note(isShopping and "No additional ingredients are required." or "No combines are required.", colors.muted)
    return
  end
  local names, groups = groupRows(rows, isShopping and "source" or "station")
  local field = isShopping and "source" or "station"
  local chosen = view[field]
  if not groups[chosen] then chosen = names[1] end
  chosen = drawGroupChoices(names, chosen, scale)
  view[field] = chosen
  local counts
  if isShopping and view.bags then
    local bags, bank, shared, isBankLive = inventory()
    if bags then counts = {bags = bags, bank = bank, shared = shared, bankLive = isBankLive} end
    if not counts then note("Bag counts are unavailable.", colors.muted) end
  end
  for _, row in ipairs(groups[chosen] or {}) do
    drawStep(action, state, plan, row, isShopping and "material" or "combine", scale, counts,
      isShopping and view.owned and view.ownedEdits or nil)
  end
end

local function drawChoices(action, state, view, plan, scale)
  local canEdit = editable(state)
  note("Target quantity")
  ui.SetNextItemWidth(112 * scale)
  local value, quantity = editedNumber("##edit-target", view.target, canEdit, 10000)
  view.target = value
  ui.SameLine()
  if button("Update target", nil, 26 * scale, true, canEdit and quantity and quantity > 0 and quantity ~= plan.quantity and true or false) then
    change(action, state, plan, "quantity", "", quantity)
  end
  rule()
  note("Ingredient sources")
  for _, choice in ipairs(plan.choices or {}) do
    ui.PushID(choice.item_key)
    note(choice.name, colors.gold)
    local selected
    for _, option in ipairs(choice.options) do
      if option.id == choice.selected then selected = option.name; break end
    end
    ui.BeginDisabled(not canEdit)
    ui.SetNextItemWidth(-1)
    if ui.BeginCombo("##choose-source", selected or "Default") then
      for index, option in ipairs(choice.options) do
        if ui.Selectable(option.name:gsub("#", "") .. "##option" .. index, option.id == choice.selected)
            and option.id ~= choice.selected then
          change(action, state, plan, "choice", choice.item_key, option.id)
        end
      end
      ui.EndCombo()
    end
    ui.EndDisabled()
    ui.PopID()
  end
  if #(plan.choices or {}) == 0 then note("No alternative sources for this plan.", colors.muted) end
  rule()
  if view.archive then
    note("Archive this plan from your saved list?", colors.warning)
    if button("Archive plan", nil, 26 * scale, true, canEdit) then
      change(action, state, plan, "archive", "", "1")
      view.requestedPlan = nil
      view.archive = false
    end
    ui.SameLine()
    if button("Keep plan", nil, 26 * scale, false, true) then view.archive = false end
  elseif button("Review archive", nil, 26 * scale, false, canEdit) then
    view.archive = true
  end
end

local function drawPlan(action, state, view, scale)
  drawQueued(action, state, view, scale)
  if view.import then
    heading("Import browser saves")
    note("On Bastiongame.com, open Saved Tradeskill Plans in the browser that holds your saves. Sign in and choose Copy to my account.")
    note("Your browser copy stays available. Refresh here after copying.", colors.muted)
    if button("Back to plan", nil, 27 * scale, false, true) then view.import = false end
    return
  end
  local plan = state.plan
  if not plan then
    heading("Tradeskill Plans")
    note(state.pending and "Loading your plan…" or "Browse recipes to create a plan, or select a saved plan.", colors.muted)
    return
  end
  if view.planId ~= plan.id then
    view.planId, view.target, view.ownedEdits = plan.id, tostring(plan.quantity), {}
    view.source, view.station, view.archive = nil, nil, false
  end
  if view.target == view.targetReported then view.target = tostring(plan.quantity) end
  view.targetReported = tostring(plan.quantity)
  local subtitle = details(plan.skill, plan.trivial)
  local pinned = view.pin == plan.id
  local function title()
    heading(plan.name)
    note((subtitle ~= "" and subtitle .. "  /  " or "") .. plan.quantity .. " target " .. (plan.quantity == 1 and "unit" or "units"), colors.muted)
  end
  local function togglePin()
    if not button(pinned and "Unpin tracker" or "Pin tracker", nil, 26 * scale, pinned, true) then return end
    if pinned then
      unpin(action, view)
    elseif accepted(action, "pin", tostring(plan.id)) then
      view.pin, view.pinnedPlan = plan.id, plan
    end
  end
  if ui.GetContentRegionAvail() >= 370 * scale and ui.BeginTable("##plan-header", 2, ui.TableFlags.SizingStretchProp) then
    ui.TableSetupColumn("##heading", ui.TableColumnFlags.WidthStretch, 1)
    ui.TableSetupColumn("##pin", ui.TableColumnFlags.WidthFixed, 112 * scale)
    ui.TableNextRow();ui.TableNextColumn();title();ui.TableNextColumn();togglePin();ui.EndTable()
  else
    title();togglePin()
  end
  if type(plan.warning) == "string" and plan.warning ~= "" then note(plan.warning, colors.gold) end
  progress(plan, scale)
  drawTabs(view, scale)
  if view.tab == 3 then drawChoices(action, state, view, plan, scale)
  else drawSteps(action, state, view, plan, scale) end
end

local function split(id, scale, leftSize, minimum, left, right)
  local width, height = ui.GetContentRegionAvail()
  if width < minimum * scale then
    child(id .. "-left", 0, math.max(72 * scale, math.min(155 * scale, height * 0.38)), left)
  else
    child(id .. "-left", leftSize * scale, 0, left)
    ui.SameLine()
  end
  child(id .. "-right", 0, 0, right)
end

local function drawWorkspace(action, state, view, scale)
  note("BASTION", colors.gold)
  rule()
  split("##tradeskill-plans", scale, 192, 650,
    function() drawSidebar(action, state, view, scale) end,
    function() drawPlan(action, state, view, scale) end)
end

local function chooseRecipe(view, recipe)
  view.recipe = recipe
  view.draftQuantity = "1"
  view.productKey = nil
  view.waitingForCreate = nil
end

local function drawCatalog(action, state, view, scale)
  for _, recipe in ipairs(state.catalog or {}) do
    if itemButton(recipe.name, recipe.id, scale, view.recipe and view.recipe.id == recipe.id, ready(state)) then
      if not view.recipe or recipe.id ~= view.recipe.id then chooseRecipe(view, recipe) end
    end
    note(details(recipe.skill, recipe.trivial, recipe.station), colors.muted)
  end
  if #(state.catalog or {}) == 0 then
    note(state.pending and "Searching the Library…" or "No recipes found.", colors.muted)
  end
  if view.page > 1 or state.catalog_more then
    rule()
    local canPage = ready(state) and view.query == view.lastQuery and view.skill == view.lastSkill
    if button("Previous page", nil, 26 * scale, false, canPage and view.page > 1) then
      searchPage(action, view, view.page - 1)
    end
    ui.SameLine()
    if button("Next page", nil, 26 * scale, false, canPage and state.catalog_more == true) then
      searchPage(action, view, view.page + 1)
    end
  end
end

local function drawDraft(action, state, view, scale)
  if not view.recipe then
    heading("Plan a recipe")
    note("Select a recipe to set a target and preview the ingredients.", colors.muted)
    return
  end
  local recipe = view.recipe
  heading(recipe.name)
  note(details(recipe.skill, recipe.trivial, recipe.station), colors.muted)
  rule()
  local preview = state.preview
  if preview and preview.recipe_id == recipe.id and preview.products and #preview.products > 1 then
    local currentKey = view.productKey or preview.target_item_key
    local name = "Default output"
    for _, product in ipairs(preview.products) do
      if product.item_key == currentKey then name = product.name; break end
    end
    ui.BeginDisabled(not ready(state))
    ui.SetNextItemWidth(-1)
    if ui.BeginCombo("Target item", name) then
      for index, product in ipairs(preview.products) do
        if ui.Selectable(product.name:gsub("#", "") .. "##product" .. index, product.item_key == currentKey) then
          view.productKey = product.item_key
        end
      end
      ui.EndCombo()
    end
    ui.EndDisabled()
  end
  ui.SetNextItemWidth(126 * scale)
  local value, quantity = editedNumber("Target quantity##draft", view.draftQuantity, ready(state), 10000)
  view.draftQuantity = value
  local valid = quantity ~= nil and quantity > 0
  local previewMatches = valid and preview and preview.recipe_id == recipe.id and preview.quantity == quantity
    and (not view.productKey or preview.target_item_key == view.productKey)
  local function draftAction(operation)
    if view.productKey then return accepted(action, operation, recipe.id, tostring(quantity), view.productKey) end
    return accepted(action, operation, recipe.id, tostring(quantity))
  end
  if button("Preview plan", nil, 27 * scale, false, ready(state) and valid) then
    draftAction("preview")
  end
  ui.SameLine()
  local canCreate = editable(state) and previewMatches == true
  if button("Save plan", nil, 27 * scale, true, canCreate) then
    if draftAction("create") then
      view.waitingForCreate = {id = state.plan and state.plan.id}
    end
  end
  if not previewMatches then
    note("Preview the target to see ingredients and combines.", colors.muted)
    return
  end
  rule()
  local estimate = preview.combines .. (preview.combines == 1 and " combine" or " combines")
  if preview.output then estimate = estimate .. "  /  " .. preview.output .. " items on success" end
  note(estimate, colors.gold)
  for _, material in ipairs(preview.materials or {}) do
    note(material.quantity .. "×  " .. material.name, colors.muted)
  end
end

local function drawBrowser(action, state, view, scale)
  local allow = ready(state)
  ui.SetNextItemWidth(math.min(190 * scale, ui.GetContentRegionAvail()))
  ui.BeginDisabled(not allow)
  if ui.BeginCombo("##skill-filter", view.skill ~= "" and view.skill or "All tradeskills") then
    if ui.Selectable("All tradeskills", view.skill == "") then view.skill = "" end
    for _, skill in ipairs(skills) do
      if ui.Selectable(skill, skill == view.skill) then view.skill = skill end
    end
    ui.EndCombo()
  end
  local width = ui.GetContentRegionAvail()
  if width >= 510 * scale then ui.SameLine() end
  local searchWidth = ui.GetContentRegionAvail()
  local buttonWidth = ui.CalcTextSize("Search") + 23 * scale
  local canInline = searchWidth >= buttonWidth + 160 * scale
  local inputWidth = canInline and searchWidth - buttonWidth or searchWidth
  ui.SetNextItemWidth(math.max(1, math.min(248 * scale, inputWidth)))
  view.query = ui.InputTextWithHint("##recipe-name", "Recipe name", view.query, 97)
  ui.EndDisabled()
  if canInline then ui.SameLine() end
  if button("Search", nil, 27 * scale, true, allow) then searchPage(action, view, 1) end
  visibleStatus(state)
  drawQueued(action, state, view, scale)
  rule()
  split("##recipe-browser", scale, 218, 510,
    function() drawCatalog(action, state, view, scale) end,
    function() drawDraft(action, state, view, scale) end)
end

local function drawTracker(action, state, view, scale)
  local plan = view.pinnedPlan
  if not plan then return end
  local io = ui.GetIO()
  local width = math.min(io.width - 24, 328 * scale)
  local height = math.min(io.height - 24, 228 * scale)
  ui.SetNextWindowSize(width, height, ui.Cond.FirstUseEver)
  ui.SetNextWindowPos(io.width - width - 18, math.max(0, io.height / 2), ui.Cond.FirstUseEver)
  local visible, open = ui.Begin(trackerTitle, true)
  if visible then
    note("PINNED PLAN", colors.gold)
    heading(plan.name)
    progress(plan, scale)
    local shown = 0
    local current = (state.plan and state.plan.id == plan.id)
      or (state.pinned_plan and state.pinned_plan.id == plan.id)
    for _, rows in ipairs({plan.materials or {}, plan.combines or {}}) do
      for _, step in ipairs(rows) do
        if not step.completed and step.checkable ~= false and shown < 2 then
          ui.PushID("pinned:" .. step.id)
          ui.BeginDisabled(not current or not editable(state))
          local value, changed = ui.Checkbox((step.quantity .. "×  " .. step.name):gsub("#", "") .. "##pinned", false)
          ui.EndDisabled()
          if changed and current then change(action, state, plan, "step", step.id, value and 1 or 0) end
          note(step.source or details(step.skill, nil, step.station), colors.muted)
          ui.PopID()
          shown = shown + 1
        end
      end
    end
    if shown == 0 then note("Checklist complete.", colors.green) end
    if button("Open plan", nil, 26 * scale, false, ready(state)) then openPlan(action, view, plan.id) end
  end
  ui.End()
  if not open then unpin(action, view) end
end

local function newView()
  return {tab = 1, skill = "", query = "", page = 1, draftQuantity = "1", bags = true, ownedEdits = {}}
end

function Planner.register(frame)
  local view, previousScope, lastShown = newView(), nil, false
  local cachedState, cachedRevision
  ui.window("Bastion Tradeskill Plans", function()
    local plugin = Host.Plugin("bastion")
    local action = plugin and plugin.tradeskill_plans
    local visibility = action and action("visibility")
    local scope = visibility and visibility.scope
    if not visibility or not visibility.active or type(scope) ~= "string" or scope == "" then
      -- Keep the pin and local edits while zoning. Never draw or retain a cached
      -- plan or inventory snapshot until the server confirms the owner again.
      view.pinnedPlan, view.requestedPlan, lastShown = nil, nil, false
      cachedState, cachedRevision = nil, nil
      clearInventory()
      if visibility and visibility.active and visibility.shown then
        local function status(scale)
          heading("Tradeskill Plans")
          local message = visibility.message
          if visibility.pending then
            message = "Connecting to your Bastion account…"
          elseif type(message) ~= "string" or message == "" then
            message = "The account planner is unavailable. Check your linked account and try again."
          end
          note(message, colors.muted)
          if button("Retry connection", nil, 27 * scale, true, not visibility.pending) then action("refresh") end
        end
        if frame then
          local visible = frame.begin("Tradeskill Plans Connection", 470, 210)
          if visible then styles(frame.scale); status(frame.scale); unstyle() end
          if frame.finish() then action("close") end
        else
          local scale = math.max(0.65, math.min(2, ui.GetIO().uiScale or 1))
          styles(scale)
          local visible, open = openWindow("Tradeskill Plans##tradeskill-connection", 470, 210, scale, false)
          if visible then status(scale) end
          ui.End()
          unstyle()
          if not open then action("close") end
        end
      end
      return
    end
    if previousScope ~= scope then
      view, previousScope, lastShown = newView(), scope, false
      cachedState, cachedRevision = nil, nil
      clearInventory()
    end
    if not visibility.shown and not view.pin then lastShown = false; return end
    if not cachedState or visibility.revision == nil or cachedRevision ~= visibility.revision then
      cachedState = action("state")
      cachedRevision = visibility.revision
    end
    local state = cachedState
    if not state then return end
    if visibility.shown and not state.plan and state.plans and state.plans[1] and editable(state)
        and not view.requestedPlan then
      local id = state.plans[1].id
      for _, plan in ipairs(state.plans) do
        if plan.id == view.planId then id = plan.id; break end
      end
      openPlan(action, view, id)
    end
    if view.pin then
      if state.plan and state.plan.id == view.pin then
        view.pinnedPlan = state.plan
      elseif state.pinned_plan and state.pinned_plan.id == view.pin then
        view.pinnedPlan = state.pinned_plan
      elseif state.fresh and state.plans then
        local stillSaved = false
        for _, plan in ipairs(state.plans) do if plan.id == view.pin then stillSaved = true; break end end
        if not stillSaved then unpin(action, view) end
      end
    end
    if view.waitingForCreate and ready(state) and not state.queued then
      local plan = state.plan
      if plan and plan.id ~= view.waitingForCreate.id then view.browser = false end
      view.waitingForCreate = nil
    end
    local scale = math.max(0.65, math.min(2, ui.GetIO().uiScale or 1))
    if visibility.shown then
      if frame then
        local visible = frame.begin("Tradeskill Plans", 880, 525)
        if visible then
          styles(frame.scale)
          drawWorkspace(action, state, view, frame.scale)
          unstyle()
        end
        if frame.finish() then action("close"); view.browser = false end
      else
        styles(scale)
        local visible, open = openWindow(mainTitle, 880, 525, scale, not lastShown)
        if visible then drawWorkspace(action, state, view, scale) end
        ui.End()
        unstyle()
        if not open then action("close"); view.browser = false end
      end
    end
    lastShown = visibility.shown
    if visibility.shown and view.browser then
      if frame then
        local visible = frame.begin("Browse recipes", 680, 420)
        if visible then
          styles(frame.scale)
          drawBrowser(action, state, view, frame.scale)
          unstyle()
        end
        if frame.finish() then view.browser = false end
      else
        styles(scale)
        local visible, open = openWindow(browserTitle, 674, 420, scale, view.focusBrowser)
        if visible then drawBrowser(action, state, view, scale) end
        ui.End()
        unstyle()
        view.browser = open
      end
      view.focusBrowser = false
    end
    if view.pin then
      styles(scale)
      drawTracker(action, state, view, scale)
      unstyle()
    end
  end)
end

return Planner
