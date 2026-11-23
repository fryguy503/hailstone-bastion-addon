local root=ADDON_ROOT or(arg and arg[1])or"."
local setup=assert(loadfile(root.."/tests/received_tools_ui_harness.lua"))()
local checks=0
local function check(ok,why)checks=checks+1;assert(ok,why)end
for _,pack in ipairs({"modern","gamepad"})do
  local windows,clicks,changes,calls={},{},{},{}
  local visibleWindows,labels={},{}
  local fallback={entries={}}
  local stateReads=0
  local s={active=true,ready=true,markers=2,local_count=0,settings={enabled=1,opacity=100,unified_colors=1,contrast_edges=1,edge_width=32,
    hostile=0x9cf06b63,friendly=0x944fa9f5,neutral=0x9ce6bd67,safe=0x944ed6aa}}
  local p={telegraphs=function(op,...)
    if op=="fallback"then return fallback end
    if op=="state"then stateReads=stateReads+1 else calls[#calls+1]={op,...}end
    return s
  end}
  BASTION_UI={};eq={Plugin=function()return p end}
  ui={window=function(name,fn)windows[name]=fn end,Begin=function(title,_,flags)visibleWindows[title]=flags or 0;return true,true end,
    End=function()end,SetNextWindowBgAlpha=function()end,
    BeginChild=function()return true end,EndChild=function()end,SameLine=function()end,TextWrapped=function(value)labels[#labels+1]=value end,
    Button=function(label)return clicks[label]end,BeginCombo=function()return false end,EndCombo=function()end,
    SetNextItemWidth=function()end,Checkbox=function(label,value)if changes[label]~=nil then return changes[label],true end;return value,false end,
    SliderInt=function(label,value)if changes[label]~=nil then return changes[label],true end;return value,false end}
  local h=setup(root,pack,ui,eq,check)
  dofile(root.."/ui/"..pack.."/telegraphs.lua")
  local function frame()h.advance();windows["Bastion Telegraphs"]();h.balanced();clicks={};changes={}end
  frame();check(#calls==0,"hidden telegraph controls sent commands")
  BASTION_UI.telegraphs=true;frame();check(#calls==0,"gallery emitted preview automatically")
  clicks["Preview shapes"]=true;frame();check(calls[#calls][1]=="command"and calls[#calls][2]=="test circle hostile solid noarrows","preview lost shape/role/pattern")
  calls={};clicks["Appearance##telegraph-page"]=true;frame();check(#calls==0,"appearance tab changed settings")
  changes["Use unified encounter palette"]=false;frame();check(calls[#calls][1]=="set"and calls[#calls][2]=="unified_colors"and calls[#calls][3]==0,"unified palette control failed")
  changes["Fine outlines, clear glyphs and impact clock"]=false;frame();check(calls[#calls][2]=="contrast_edges"and calls[#calls][3]==0,"contrast control failed")
  changes["Edge width"]=48;frame();check(calls[#calls][2]=="edge_width"and calls[#calls][3]==48,"edge width control failed")
  clicks["High contrast palette"]=true;frame();check(calls[#calls][1]=="preset"and calls[#calls][2]=="contrast","palette preset failed")
  clicks["Renderer status##telegraph-page"]=true;frame()
  clicks["Refresh server markers"]=true;frame();check(calls[#calls][2]=="hello","manual snapshot request failed")
  h.width=1000;h.height=600;frame();h.width=1920;h.height=1080;frame()
  s.active=false;local actions=#calls;frame();check(not BASTION_UI.telegraphs and #calls==actions,"Inactive telegraphs retained the open flag or sent an action")
  local before=stateReads;for _=1,120 do frame()end
  check(stateReads==before,"Invisible retired telegraph settings kept polling state")
  s.active=true;frame();check(stateReads==before,"Telegraph settings reopened implicitly after peer restoration")
  BASTION_UI.telegraphs=true;frame();check(stateReads==before+1 and #calls==actions,"Explicit telegraph reopen missed current state or sent a request")
  local loaded=p;p=nil;frame();check(not BASTION_UI.telegraphs,"Missing telegraph provider retained its local open flag")
  p=loaded;BASTION_UI.telegraphs=true
  h.close=true;frame();check(not BASTION_UI.telegraphs,"close ignored")

  local alertWindow="Bastion Encounter Warnings###bastion_telegraph_fallback"
  local function drawAlerts()
    h.advance();visibleWindows={};labels={}
    windows["Bastion Encounter Warnings"]()
    h.balanced()
    return table.concat(labels,"\n")
  end
  local focusCount,actionCount=h.focused,#calls
  check(drawAlerts()==""and next(visibleWindows)==nil,"Empty fallback opened an alert window")
  fallback.entries={
    {id=1,shape=9,mechanic=4,disposition=0,remaining_ms=3170,reason="missing-anchor"},
    {id=2,shape=7,mechanic=2,disposition=4,remaining_ms=1100,distance=24,direction="left",reason="terrain"},
  }
  local text=drawAlerts()
  check(not BASTION_UI.telegraphs and text:find("Danger  /  Move away",1,true)and
    text:find("Source location unavailable",1,true)and text:find("3.2 s to impact",1,true),
    "Missing anchor did not show its mechanic, role and countdown without opening settings")
  check(text:find("Safe  /  Gather",1,true)and text:find("24 paces left",1,true),
    "Fallback lost a safe marker's role or trusted distance and direction")
  check(visibleWindows[alertWindow]&ui.WindowFlags.NoInputs~=0 and
    visibleWindows[alertWindow]&ui.WindowFlags.NoFocusOnAppearing~=0 and h.focused==focusCount,
    "Ordinary alert intercepted input or moved focus")
  table.remove(fallback.entries,1)
  text=drawAlerts()
  check(not text:find("Move away",1,true)and text:find("Safe",1,true),
    "A resolved marker remained in the alert window")
  for n=3,24 do fallback.entries[#fallback.entries+1]={id=n,mechanic=3,remaining_ms=2000,distance=100+n}end
  h.width=640;h.height=480
  text=drawAlerts()
  check(text:find("Scroll for all 23 warnings",1,true)and text:find("103 paces",1,true)and text:find("124 paces",1,true)and
    visibleWindows[alertWindow]&ui.WindowFlags.NoInputs==0 and h.focused==focusCount,
    "Overlapping warnings were dropped, unscrollable or stole focus")
  fallback.entries={}
  check(drawAlerts()==""and next(visibleWindows)==nil and #calls==actionCount,
    "Expired alerts left a window or sent a game action")
  p=nil
  check(drawAlerts()==""and next(visibleWindows)==nil,"Missing plugin left a stale fallback alert")
  h.finish()
end
print("Telegraphs UI PASS "..checks.." checks")
