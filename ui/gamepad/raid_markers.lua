local Host = dofile("lib/bastion_host.lua")

-- Same colors and normalized glyphs as the world markers. Each row contains line endpoints.
local glyphs = {
  {"Sunburst", 1,.72,.25, {{-1,0,1,0}, {0,-1,0,1}, {-.65,-.65,.65,.65}, {-.65,.65,.65,-.65}}},
  {"Cairn", 1,.41,.30, {{-1,-.65,0,1}, {0,1,1,-.65}, {1,-.65,-1,-.65}}},
  {"Wave", .22,.81,1, {{-1,-.5,-.5,.55}, {-.5,.55,0,-.5}, {0,-.5,.5,.55}, {.5,.55,1,-.5}}},
  {"Kite", .80,.49,1, {{-1,0,0,1}, {0,1,1,0}, {1,0,0,-1}, {0,-1,-1,0}}},
  {"Fork", 1,.44,.74, {{-.85,.8,0,0}, {.85,.8,0,0}, {0,0,0,-1}}},
  {"Grove", .42,.93,.51, {{-.8,-.8,-.8,.8}, {0,-.8,0,1}, {.8,-.8,.8,.8}, {-.8,-.8,.8,-.8}}},
  {"Gate", .41,.60,1, {{-.85,-.8,-.85,.35}, {-.85,.35,-.35,.85}, {-.35,.85,.35,.85},
                        {.35,.85,.85,.35}, {.85,.35,.85,-.8}}},
  {"Orbit", .87,.94,.98, {{-.9,-.4,-.9,.4}, {-.9,.4,-.4,.9}, {-.4,.9,.4,.9}, {.4,.9,.9,.4},
                         {.9,.4,.9,-.4}, {.9,-.4,.4,-.9}, {.4,-.9,-.4,-.9}, {-.4,-.9,-.9,-.4}}},
}

local position_key = "hud.pos.bastionRaidMarkers"
local positioned, saved_position, shown_position
local action_error = ""

local function draw_glyph(glyph, scale, placed, ready)
  local x0, y0, x1, y1 = ui.GetItemRect()
  local x, y = (x0+x1)/2, (y0+y1)/2 - (placed and 2*scale or 0)
  local size = 7*scale
  local r, g, b = glyph[2], glyph[3], glyph[4]
  local alpha = ready and 1 or .45
  for _, line in ipairs(glyph[5]) do
    ui.DrawLine(x+line[1]*size, y-line[2]*size, x+line[3]*size, y-line[4]*size, r,g,b,alpha,1.8*scale)
  end
  if placed then ui.DrawLine(x-5*scale,y1-3*scale,x+5*scale,y1-3*scale,r,g,b,alpha,1.8*scale) end
end

local function tooltip(text)
  local allow_disabled = (ui.HoveredFlags or {}).AllowWhenDisabled or 0
  if ui.IsItemHovered(allow_disabled) then ui.SetTooltip(text) end
end

local function place_window(vw, vh, width, scale)
  local setting = ui.GetSetting(position_key) or ""
  if positioned and setting == saved_position then return end
  local x, y = setting:match("^(-?%d+),(-?%d+)$")
  x = math.max(0, math.min(tonumber(x) or (vw-width)/2, vw-width))
  y = math.max(0, math.min(tonumber(y) or vh*.70, vh-82*scale))
  ui.SetNextWindowPos(x, y, ui.Cond.Always)
  positioned, saved_position, shown_position = true, setting, nil
end

local function save_position()
  local x, y = ui.GetWindowPos()
  local current = string.format("%d,%d", math.floor(x+.5), math.floor(y+.5))
  if not shown_position then shown_position = current end
  if shown_position ~= current and not ui.IsMouseDown(ui.Mouse.Left) then
    if ui.SetSetting(position_key, current) then saved_position = current end
    shown_position = current
  end
end

local function activate(call, operation, marker)
  local response
  if marker then response = call(operation, marker) else response = call(operation) end
  if type(response) == "table" and response.ok == 1 then
    action_error = ""
  else
    action_error = type(response) == "table" and response.error or "Raid marker request unavailable."
  end
end

ui.window("Bastion Raid Markers", function()
  local plugin = Host.Plugin("bastion")
  local call = plugin and plugin.raid_markers
  local state = type(call) == "function" and call("state")
  local ready = type(state) == "table" and state.can_place == 1
  local visible_to_role = type(state) == "table" and
    (state.show_bar == 1 or (state.show_bar == nil and ready))
  if not visible_to_role or state.available ~= 1 then
    positioned, action_error = false, ""
    return
  end

  local io = ui.GetIO()
  local vw, vh = io.width or 1280, io.height or 720
  local scale = math.max(.55, math.min(2, io.uiScale or 1, vw/322))
  local size, spacing = 26*scale, 4*scale
  local width = 318*scale
  place_window(vw, vh, width, scale)
  ui.PushStyleVar(ui.StyleVar.WindowPadding, 7*scale, 6*scale)
  ui.PushStyleVar(ui.StyleVar.ItemSpacing, spacing, spacing)
  ui.SetNextWindowSize(width, 0, ui.Cond.Always)
  local w = ui.WindowFlags
  local visible = ui.Begin("Raid Markers###bastionRaidMarkers", nil,
    w.NoSavedSettings | w.NoCollapse | w.NoResize | w.NoFocusOnAppearing | (w.AlwaysAutoResize or 0))
  if visible then
    save_position()
    local slots = state.slots or {}
    local aiming = ready and glyphs[state.aiming] ~= nil
    local waiting = type(state.status) == "string" and state.status ~= "" and state.status or "Waiting for raid leader."
    local any_placed = false
    for index, glyph in ipairs(glyphs) do
      if index > 1 then ui.SameLine() end
      local placed = slots[index] == 1
      any_placed = any_placed or placed
      local selected = aiming and state.aiming == index
      if selected then ui.PushStyleColor(ui.Col.Button, .24,.35,.43,1) end
      ui.BeginDisabled(not ready)
      local clicked = ui.Button("##raidMarker"..index, size, size)
      ui.EndDisabled()
      if selected then ui.PopStyleColor() end
      draw_glyph(glyph, scale, placed, ready)
      local action = selected and "Cancel " or placed and "Move " or "Place "
      tooltip(ready and action .. glyph[1] or waiting)
      if clicked and ready then
        if selected then activate(call, "cancel") else activate(call, "aim", index) end
      end
    end
    ui.SameLine()
    if aiming then
      if ui.Button("Cancel##raidMarkers", 56*scale, size) then activate(call, "cancel") end
      tooltip("Cancel placement")
      ui.Text("Click ground to place. Esc cancels.")
    else
      ui.BeginDisabled(not ready or not any_placed)
      local clear = ui.Button("Clear##raidMarkers", 56*scale, size)
      ui.EndDisabled()
      tooltip(not ready and waiting or any_placed and "Clear one marker or clear all" or "No raid markers are placed")
      if clear and ready and any_placed then ui.OpenPopup("##bastionRaidMarkerClear") end
      if ui.BeginPopup("##bastionRaidMarkerClear") then
        if ready and any_placed then
          for index, glyph in ipairs(glyphs) do
            if slots[index] == 1 and ui.MenuItem("Clear "..glyph[1].."##clearRaidMarker"..index) then
              activate(call, "clear", index)
            end
          end
          if ui.MenuItem("Clear all##raidMarkers") then activate(call, "clear_all") end
        else
          ui.CloseCurrentPopup()
        end
        ui.EndPopup()
      end
      if not ready then ui.Text(waiting) end
    end
    local error_text = action_error ~= "" and action_error or state.error
    if type(error_text) == "string" and error_text ~= "" then
      ui.PushTextWrapPos(0)
      ui.TextColored(1,.66,.46,1,error_text)
      ui.PopTextWrapPos()
    end
  end
  ui.End()
  ui.PopStyleVar(2)
end)
