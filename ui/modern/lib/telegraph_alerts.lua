-- Visual backup for server warnings whose ground geometry could not be drawn.
local C = dofile("lib/citadel_style.lua")

local roles = {
  {"Danger", "danger", "Avoid the area"},
  {"Friendly", "blue", "Find the source"},
  {"Caution", "warn", "Check the source"},
  {"Safe", "good", "Move to the safe area"},
}
local actions = {
  "Avoid the area", "Gather at the source", "Spread apart", "Move away from the source",
  "Soak the marked area", "Split between the marked areas", "Look away from the source",
  "Prepare for knockback", "Interrupt the cast",
}
local shapeMechanics = {[7]=2, [8]=3, [9]=4, [10]=7}
local inferredRoles = {1, 2, 1, 1, 2, 3, 1, 1, 1}
local directions = {left="left", right="right", ahead="ahead", behind="behind"}
local header = "Ground indicators unavailable"

local function finite(value)
  return type(value) == "number" and value >= 0 and value < math.huge
end

local function describe(entry)
  local mechanic = actions[entry.mechanic] and entry.mechanic or shapeMechanics[entry.shape]
  local role = roles[entry.disposition] or roles[inferredRoles[mechanic]] or roles[3]
  local direction = directions[entry.direction]
  local source = direction or "Source location unavailable"
  if finite(entry.distance) then
    source = string.format("%.0f paces", entry.distance) .. (direction and " " .. direction or " to the source")
  end
  local time = ""
  if finite(entry.remaining_ms) then
    time = entry.remaining_ms == 0 and "   |   Impact" or
      string.format("   |   %.1f s to impact", math.ceil(entry.remaining_ms / 100) / 10)
  end
  return {
    title = role[1] .. "  /  " .. (actions[mechanic] or role[3]),
    detail = source .. time,
    tone = role[2],
  }
end

return function(state)
  local entries = type(state) == "table" and state.entries
  if type(entries) ~= "table" or #entries == 0 then return end

  local io, scale = ui.GetIO(), C.scale()
  local vw, vh = io.width or 1280, io.height or 720
  if vw <= 0 or vh <= 0 then return end
  local margin = math.min(16 * scale, vw / 8, vh / 8)
  local width = math.min(460 * scale, vw - 2 * margin)
  local lineWidth = math.max(1, width - 48 * scale)
  local function heightFor(text)
    local textWidth, lineHeight = ui.CalcTextSize(text)
    return math.max(1, math.ceil(textWidth / lineWidth)) * math.max(1, lineHeight or 18 * scale) + 7 * scale
  end
  local rows = {}
  local height = 24 * scale + heightFor(header)
  for _, entry in ipairs(entries) do
    local row = describe(entry)
    rows[#rows + 1] = row
    height = height + heightFor(row.title) + heightFor(row.detail)
  end

  -- Keep the combat view clear when the alerts fit. The full list can scroll
  -- when many warnings overlap, without automatically taking keyboard focus.
  local maxHeight = math.min(vh - 2 * margin, math.max(118 * scale, vh * .48))
  local overflow = height > maxHeight
  height = math.min(height, maxHeight)
  local w = ui.WindowFlags
  local flags = w.NoSavedSettings | w.NoTitleBar | w.NoCollapse | w.NoMove | w.NoResize |
    w.NoFocusOnAppearing | (w.NoBringToFrontOnFocus or 0)
  if not overflow then flags = flags | w.NoInputs end
  ui.SetNextWindowPos((vw - width) / 2, margin, ui.Cond.Always)
  ui.SetNextWindowSize(width, height, ui.Cond.Always)
  ui.SetNextWindowBgAlpha(.98)
  C.push()
  if ui.Begin("Bastion Encounter Warnings###bastion_telegraph_fallback", nil, flags) then
    C.note(header, "gold")
    if overflow then C.note("Scroll for all " .. #rows .. " warnings", "muted") end
    for _, row in ipairs(rows) do
      C.note(row.title, row.tone)
      C.note(row.detail, "text")
    end
  end
  ui.End()
  C.pop()
end
