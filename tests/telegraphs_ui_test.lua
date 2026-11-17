local root=ADDON_ROOT or(arg and arg[1])or"."
local setup=assert(loadfile(root.."/tests/received_tools_ui_harness.lua"))()
local checks=0
local function check(ok,why)checks=checks+1;assert(ok,why)end
for _,pack in ipairs({"modern","gamepad"})do
  local draw,clicks,changes,calls=nil,{},{},{}
  local stateReads=0
  local s={active=true,ready=true,markers=2,local_count=0,settings={enabled=1,opacity=100,unified_colors=1,contrast_edges=1,edge_width=32,
    hostile=0x9cf06b63,friendly=0x944fa9f5,neutral=0x9ce6bd67,safe=0x944ed6aa}}
  local p={telegraphs=function(op,...)if op=="state"then stateReads=stateReads+1 else calls[#calls+1]={op,...}end;return s end}
  BASTION_UI={};eq={Plugin=function()return p end}
  ui={window=function(_,fn)draw=fn end,Begin=function()return true,true end,End=function()end,
    BeginChild=function()return true end,EndChild=function()end,SameLine=function()end,TextWrapped=function()end,
    Button=function(label)return clicks[label]end,BeginCombo=function()return false end,EndCombo=function()end,
    SetNextItemWidth=function()end,Checkbox=function(label,value)if changes[label]~=nil then return changes[label],true end;return value,false end,
    SliderInt=function(label,value)if changes[label]~=nil then return changes[label],true end;return value,false end}
  local h=setup(root,pack,ui,eq,check)
  dofile(root.."/ui/"..pack.."/telegraphs.lua")
  local function frame()h.advance();draw();h.balanced();clicks={};changes={}end
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
  h.finish()
end
print("Telegraphs UI PASS "..checks.." checks")
