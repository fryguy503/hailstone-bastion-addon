local root=ADDON_ROOT or(arg and arg[1])or"."
local setup=assert(loadfile(root.."/tests/received_tools_ui_harness.lua"))()
local checks=0
local function check(ok,why)checks=checks+1;assert(ok,why)end
for _,pack in ipairs({"modern","gamepad"})do
  local draw,clicks,calls,texts=nil,{},{},{}
  local stateReads=0
  local s={active=1,ready=1,equipped="123.4 (provisional)",owned="145.6 (stale)",attempts=2,can_retry=0,templates=15,configurations=20}
  local p={gear_score=function(op)if op=="state"then stateReads=stateReads+1 else calls[#calls+1]=op end;return s end}
  BASTION_UI={};eq={Plugin=function()return p end}
  ui={window=function(_,fn)draw=fn end,Begin=function()return true,true end,End=function()end,
    BeginChild=function()return true end,EndChild=function()end,SameLine=function()end,
    TextWrapped=function(v)texts[#texts+1]=v end,Button=function(label)return clicks[label]end}
  local h=setup(root,pack,ui,eq,check)
  dofile(root.."/ui/"..pack.."/gear_score.lua")
  local function frame()h.advance();texts={};draw();h.balanced();clicks={}end
  frame();check(#texts==0 and #calls==0,"hidden summary work")
  BASTION_UI.gear_score=true;frame()
  check(table.concat(texts," "):find(s.equipped,1,true)and table.concat(texts," "):find(s.owned,1,true),"server labels changed")
  clicks["Retry item ratings"]=true;frame();check(#calls==0,"ready catalog reset")
  s.ready=0;s.can_retry=1;frame();check(#calls==0,"passive UI retried")
  clicks["Retry item ratings"]=true;frame();check(calls[1]=="retry","explicit retry missing")
  s.active=0;frame();check(#texts==0 and not BASTION_UI.gear_score,"inactive peer displayed ratings or retained its open flag")
  local before=stateReads;for _=1,120 do frame()end
  check(stateReads==before,"Invisible retired item ratings kept polling state")
  s.active=1;frame();check(stateReads==before,"Item ratings reopened implicitly after peer restoration")
  s.equipped="New current rating";BASTION_UI.gear_score=true;frame()
  check(stateReads==before+1 and table.concat(texts," "):find(s.equipped,1,true),"Explicit item rating reopen missed current data")
  local loaded=p;p=nil;frame();check(not BASTION_UI.gear_score,"Missing item rating provider retained its local open flag")
  p=loaded;BASTION_UI.gear_score=true;h.close=true;frame();check(not BASTION_UI.gear_score,"close ignored")
  h.finish()
end
print("Item ratings UI PASS "..checks.." checks")
