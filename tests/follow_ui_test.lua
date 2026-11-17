ADDON_ROOT = ADDON_ROOT or (arg and arg[1]) or "."
local checks = 0
local function check(value, why) checks=checks+1; assert(value, why) end
for _, pack in ipairs({"modern", "gamepad"}) do
  local windows, clicks, calls, changed = {}, {}, {}, {}
  local stateReads=0
  local state = {active=1, enabled=1, cancel_on_move=1, instant_mount=1, distance=15, can_start=1, following=0, selected_name="Peer", selected_distance=12}
  local plugin = {citadel_follow=function(operation, ...)
    if operation == "state" then stateReads=stateReads+1;return state end
    calls[#calls+1]={operation, ...}; return {ok=1, error=""}
  end}
  local disabled, closes, focused = false, false, 0
  BASTION_UI = nil
  eq = setmetatable({Plugin=function(name) check(name == "bastion", "plugin identity"); return plugin end}, {
    __index=function(_, name) error("Unexpected game action API " .. name) end})
  local canonical=assert(loadfile(ADDON_ROOT.."/tests/host_ui_api_mock.lua"))()(eq,check)
  local function button(label)
    local yes=clicks[label]; clicks[label]=nil; return yes == true and not disabled
  end
  local enum=setmetatable({},{__index=function()return 1 end})
  ui = {Cond={FirstUseEver=1},Col=enum,StyleVar=enum,GetIO=function()return{uiScale=1,width=1280,height=720}end,window=function(name, fn) windows[name]=fn end,
    SetNextWindowSize=function() end,SetNextWindowPos=function()end, Begin=function() return true, true end, End=function() end,
    TextWrapped=function() end,TextColored=function()end, SameLine=function() end, Separator=function() end,
    PushStyleColor=function()end,PopStyleColor=function()end,PushStyleVar=function()end,PopStyleVar=function()end,
    PushFont=function()end,PopFont=function()end,PushTextWrapPos=function()end,PopTextWrapPos=function()end,
    GetContentRegionAvail=function()return 700,480 end,CalcTextSize=function(value)return #value*7,14 end,IsItemHovered=function()return false end,
    GetCursorScreenPos=function()return 0,0 end,
    GetWindowPos=function()return 0,0 end,SetCursorPos=function()end,Dummy=function()end,DrawLine=function()end,
    DrawCircle=function()end,DrawCircleFilled=function()end,DrawText=function()end,
    SetNextItemWidth=function() end, BeginChild=function() return true end, EndChild=function() end,
    BeginDisabled=function(value) disabled=value end, EndDisabled=function() disabled=false end,
    Button=button,
    Checkbox=function(label, old) if changed[label] ~= nil then local v=changed[label]; changed[label]=nil; return v,true end; return old,false end,
    SliderFloat=function(_, old) if changed.distance then local v=changed.distance; changed.distance=nil; return v,true end; return old,false end}
  local T={S=2/3, W=1280, H=720, frame=function() end, px=function(n) return n*2/3 end}
  T.button=function(_, label, _, _, opts)
    disabled=opts and opts.disabled or false
    local result=button(label); disabled=false; return result
  end
  local W={T=T,begin=function(_, opts)
    focused=focused+1
    check(opts.w*T.S <= T.W-40 and opts.h*T.S <= T.H-40, "viewport clamp")
    return true
  end, area=function() return 0,0,650,480,650,480 end, finish=function() return closes end}
  local old=dofile
  dofile=function(path)
    if path == "lib/theme.lua" then return T end
    if path == "lib/window.lua" then return W end
    if path == "lib/pad.lua" then return {} end
    return assert(loadfile(ADDON_ROOT.."/ui/"..pack.."/"..path))()
  end
  assert(loadfile(ADDON_ROOT .. "/ui/" .. pack .. "/citadel_follow.lua"))()
  local draw=windows["Bastion Follow"]; check(type(draw) == "function", "window registered")
  draw(); check(#calls == 0, "closed panel cannot act")
  BASTION_UI.citadel_follow=true
  draw(); check(#calls == 0, "draw is passive")
  clicks["Follow selected player"]=true
  draw(); check(#calls == 1 and calls[1][1] == "start", "manual typed start")
  state.can_start=0
  clicks["Follow selected player"]=true
  draw(); check(#calls == 1, "invalid target disables start")
  state.following=1; clicks["Stop following"]=true
  draw(); check(calls[#calls][1] == "stop", "manual stop")
  changed["Enhanced follow"]=false; changed.distance=23.25
  draw()
  check(calls[#calls-1][1] == "set" and calls[#calls-1][2] == "enabled" and calls[#calls-1][3] == 0, "data setting only")
  check(calls[#calls][1] == "set" and calls[#calls][2] == "distance" and calls[#calls][3] == 23.25, "distance preserved")
  local beforeRetire=#calls;state.active=0;clicks["Follow selected player"]=true;draw()
  check(#calls==beforeRetire and not BASTION_UI.citadel_follow,"Retired server panel accepted an action or retained its open flag")
  local beforeReads=stateReads;for _=1,120 do draw()end
  check(stateReads==beforeReads,"Invisible retired follow panel kept polling state")
  clicks["Follow selected player"]=nil;state.active=1
  draw();check(stateReads==beforeReads,"Follow settings reopened implicitly after peer restoration")
  BASTION_UI.citadel_follow=true;draw();check(stateReads==beforeReads+1,"Explicit follow reopen did not read current state")
  local loaded=plugin;plugin=nil;draw();check(not BASTION_UI.citadel_follow,"Missing follow provider retained its local open flag")
  plugin=loaded;BASTION_UI.citadel_follow=true
  if pack == "gamepad" then
    closes=true; draw(); check(not BASTION_UI.citadel_follow and focused > 0, "pack focus and B close route")
  end
  canonical();dofile=old
end
print("Follow UI: " .. checks .. " checks passed")
