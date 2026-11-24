local root = ADDON_ROOT or (arg and arg[1]) or "."
local checks = 0
local function check(condition, message)
  checks = checks + 1
  assert(condition, message)
end

for _, pack in ipairs({"modern", "gamepad"}) do
  local source = root .. "/ui/" .. pack .. "/"
  local windows, actions, buttons, menu = {}, {}, {}, {}
  local writes, tooltips, texts, glyph_lines = {}, {}, {}, 0
  local state = {available=1, can_place=0, aiming=0, slots={}}
  local installed, down, popup = true, false, false
  local x, y, frames, padding, disabled = 0, 0, 0, 0, false
  local setting, hover, reject = "300,200", nil, false
  local function dispatch(operation, marker, ...)
    if operation == "state" then return state end
    check(select("#", ...) == 0, "Extra native action argument")
    actions[#actions+1] = {operation, marker}
    if reject then return {ok=0, error="Permission changed"} end
    return {ok=1}
  end
  local api = setmetatable({
    window = function(name, render) windows[name] = render end,
    GetIO = function() return {width=1280, height=720, uiScale=1} end,
    GetSetting = function(key) check(key=="hud.pos.bastionRaidMarkers", "Position is outside character HUD scope"); return setting end,
    SetSetting = function(_, value) setting=value; writes[#writes+1]=value; return true end,
    SetNextWindowPos = function(px, py) x, y = px, py end,
    GetWindowPos = function() return x, y end,
    IsMouseDown = function() return down end,
    PushStyleVar = function() padding = padding+1 end,
    PopStyleVar = function(n) padding = padding-n end,
    Begin = function() frames=frames+1; return true end,
    End = function() frames=frames-1 end,
    BeginDisabled = function(value) disabled=value end,
    EndDisabled = function() disabled=false end,
    Button = function(id)
      if hover then hover.item=id end
      local clicked=buttons[id]; buttons[id]=nil
      return clicked and not disabled
    end,
    GetItemRect = function() return 0,0,26,26 end,
    IsItemHovered = function() return hover and hover.item==hover.target end,
    SetTooltip = function(value) tooltips[#tooltips+1]=value end,
    DrawLine = function() glyph_lines=glyph_lines+1 end,
    OpenPopup = function() popup=true end,
    BeginPopup = function() return popup end,
    EndPopup = function() popup=false end,
    MenuItem = function(id) local clicked=menu[id]; menu[id]=nil; return clicked end,
    Text = function(value) texts[#texts+1]=value end,
    TextColored = function(_,_,_,_,value) check(value=="Permission changed", "Missing action error message") end,
    Cond = {Always=1}, Mouse = {Left=0}, Col={Button=1}, StyleVar={WindowPadding=1,ItemSpacing=2},
    HoveredFlags = {AllowWhenDisabled=1},
    WindowFlags={NoSavedSettings=1,NoCollapse=2,NoResize=4,NoFocusOnAppearing=8,AlwaysAutoResize=16},
  }, {__index=function() return function() end end})
  local env = setmetatable({ui=api, hs={UI={plugins={Get=function() if installed then return {raid_markers=dispatch} end end}}}}, {__index=_G})
  env.dofile = function(path) return assert(loadfile(source..path,"t",env))() end
  env.dofile("raid_markers.lua")
  local render = assert(windows["Bastion Raid Markers"])
  local function frame()
    glyph_lines=0
    render()
    check(frames==0 and padding==0, "Window or style leaked after drawing")
  end

  frame()
  check(glyph_lines==0 and #actions==0, "Viewer without placement permission received a toolbar")
  state.show_bar=1
  hover={target="##raidMarker6"}
  buttons["##raidMarker6"]=true; buttons["Clear##raidMarkers"]=true; frame()
  check(glyph_lines==35 and #actions==0 and not popup, "Unsynced officer did not receive a read-only bar")
  check(texts[#texts]=="Waiting for raid leader." and tooltips[#tooltips]=="Waiting for raid leader.",
        "Read-only bar did not explain why it is waiting")
  state.show_bar=0
  state.can_place=1
  frame()
  check(glyph_lines==0 and #actions==0, "Explicit hidden state did not suppress toolbar")
  state.show_bar=1
  frame()
  check(glyph_lines==35 and x==300 and y==200 and #writes==0, "Toolbar glyphs or saved position are missing")
  hover={target="Clear##raidMarkers"}
  buttons["Clear##raidMarkers"]=true; frame()
  check(#actions==0 and not popup and tooltips[#tooltips]=="No raid markers are placed", "Empty raid Clear state is wrong")

  x,y,down=411,380,true; frame()
  check(#writes==0, "Dragging wrote the position before release")
  down=false; frame(); frame()
  check(#writes==1 and writes[1]=="411,380", "Dragging did not save exactly once")

  state.slots[6]=1
  hover={target="##raidMarker6"}
  buttons["##raidMarker6"]=true; frame()
  check(actions[1][1]=="aim" and actions[1][2]==6 and tooltips[#tooltips]=="Move Grove" and glyph_lines==36,
        "The active marker failed to show its status or request repositioning")
  state.aiming=6
  buttons["Cancel##raidMarkers"]=true; frame()
  check(actions[2][1]=="cancel" and actions[2][2]==nil, "Cancel failed to leave placement mode")
  state.aiming=0

  buttons["Clear##raidMarkers"]=true; menu["Clear Grove##clearRaidMarker6"]=true; frame()
  check(actions[3][1]=="clear" and actions[3][2]==6, "Clear discarded the exact marker slot")
  reject=true
  buttons["Clear##raidMarkers"]=true; menu["Clear all##raidMarkers"]=true; frame()
  check(actions[4][1]=="clear_all" and actions[4][2]==nil, "Clear all did not send its explicit action")
  state.can_place=0
  state.show_bar=0
  frame()
  check(glyph_lines==0 and #actions==4, "Visible toolbar or mutation survived loss of permission")
  state.can_place=1
  installed=false
  frame()
  check(glyph_lines==0 and #actions==4, "Toolbar survived plugin unload")
end

print("Raid marker UI checks passed (" .. checks .. ").")
