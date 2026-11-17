local root=ADDON_ROOT or (arg and arg[1]) or "."
local setup=assert(loadfile(root.."/tests/received_tools_ui_harness.lua"))()
local checks=0
local function check(ok,why)checks=checks+1;if not ok then error(why,2)end end
for _,pack in ipairs({"modern","gamepad"})do
  local draw,clicks,select,calls,texts=nil,{},false,{},{}
  local active,visibilityReads,stateReads=true,0,0
  local s={shown=false,available=true,pending=false,fresh=true,revision="18446744073709551614",grant="18364758544493064720",snapshot="17211839031895642640",
    message="Received",current_home=4,race=1,class_id=2,deity=3,suppressed=false,entries={{choice="10",zone_id=20,name="Destination",can_choose=true}}}
  local p={home_city=function(op,...)
    if op=="visibility"then visibilityReads=visibilityReads+1;return{active=active,shown=s.shown}end
    if op=="state"then stateReads=stateReads+1 else calls[#calls+1]={op,...}end
    if op=="close"then s.shown=false end
    return s
  end}
  BASTION_UI={};eq={Plugin=function()return p end};ui={Cond={FirstUseEver=1},window=function(_,fn)draw=fn end,
    SetNextWindowSize=function()end,Begin=function()return true,true end,End=function()end,TextWrapped=function(value)texts[#texts+1]=value end,
    BeginChild=function()return true end,EndChild=function()end,SameLine=function()end,
    Button=function(label)local hit=clicks[label];clicks[label]=nil;return hit end,
    Selectable=function(label)texts[#texts+1]=label;local value=select;select=false;return value end}
  local harness=setup(root,pack,ui,eq,check)
  dofile(root.."/ui/"..pack.."/home_city.lua")
  local function frame()harness.advance();texts={};draw();harness.balanced();clicks={};select=false end
  for _=1,120 do frame()end
  check(visibilityReads==120 and stateReads==0 and #calls==0,"hidden home city requested full destinations or performed automatic work")
  BASTION_UI.home_city=true;clicks["Review home city change"]=true;clicks["Yes, confirm home choice"]=true;frame();check(#calls==0,"missing destination was actionable")
  select=true;frame();check(#calls==0,"selection sent mutation")
  clicks["Review home city change"]=true;frame();check(#calls==0,"review committed")
  s.snapshot="17211839031895642641";clicks["Yes, confirm home choice"]=true;frame();check(#calls==0,"stale snapshot confirmed")
  clicks["Review home city change"]=true;frame();clicks["Yes, confirm home choice"]=true;frame()
  check(#calls==1 and calls[1][1]=="commit"and calls[1][2]==s.revision and calls[1][3]==s.grant and calls[1][4]==s.snapshot and calls[1][5]=="10","choice/grant tuple changed")
  calls={};clicks["Review disable reminders"]=true;frame();check(#calls==0,"reminder review mutated")
  clicks["Yes, confirm home choice"]=true;frame();check(#calls==1 and calls[1][1]=="suppress","manual suppress absent")
  calls={};s.pending=true;clicks["Review home city change"]=true;clicks["Refresh home city choices"]=true;frame();check(#calls==0,"pending duplicate")
  s.pending=false;s.entries[1].can_choose=false;clicks["Review home city change"]=true;clicks["Yes, confirm home choice"]=true;frame();check(#calls==0,"current home chosen")
  clicks["Later"]=true;frame();check(#calls==1 and calls[1][1]=="close"and not BASTION_UI.home_city,"Later persisted suppression")
  -- The six atlas tiles form one map only when all art is present. A pin is a
  -- local selection, and still requires a separate review/confirmation.
  BASTION_UI.home_city=true;s.entries[1].can_choose=true;s.entries[1].zone_id=55
  local artCalls={};local absent=false
  eq.UiAnimation=function(name)
    artCalls[#artCalls+1]=name
    if name:find("A_StartingCityMap",1,true)and not(absent and name=="A_StartingCityMap04")then return name,0,0,1,1,256,256 end
  end
  harness.images={};frame();local tiles=0
  for _,image in ipairs(harness.images)do if tostring(image.texture):find("A_StartingCityMap",1,true)then tiles=tiles+1 end end
  check(tiles==6,"map did not use every tile")
  calls={};ui.InvisibleButton=function(id)return id=="##home-map10"end;frame();check(#calls==0,"map selection changed home city")
  ui.InvisibleButton=function()return false end
  clicks["Review home city change"]=true;frame();s.grant="18364758544493064721";clicks["Yes, confirm home choice"]=true;frame();check(#calls==0,"map review survived a changed grant")
  absent=true;harness.images={};frame();tiles=0
  for _,image in ipairs(harness.images)do if tostring(image.texture):find("A_StartingCityMap",1,true)then tiles=tiles+1 end end
  check(tiles==0,"partial map was rendered as complete")
  s.grant="0";clicks["Review home city change"]=true;clicks["Review disable reminders"]=true;clicks["Yes, confirm home choice"]=true;frame()
  check(#calls==0 and not table.concat(texts," "):find("Free reset available",1,true),"zero grant advertised or used a reset")
  s.grant="18364758544493064722";s.fresh=false;frame();check(not table.concat(texts," "):find("Free reset available",1,true),"stale grant advertised available")
  s.fresh=true;s.available=false;frame();check(not table.concat(texts," "):find("Free reset available",1,true),"unavailable grant advertised available")
  s.available=true
  calls={};s.selection_revision="numeric1";s.preselected_choice="10";s.revision="numeric-list1"
  frame();check(#calls==0,"numeric preselection changed home automatically")
  s.revision="numeric-list2";frame()
  clicks["Review home city change"]=true;frame();check(#calls==0,"numeric preselection review committed")
  clicks["Yes, confirm home choice"]=true;frame();check(#calls==1 and calls[1][1]=="commit"and calls[1][5]=="10","numeric selected choice did not require/retain explicit confirmation")
  calls={};s.selection_revision="numeric-missing";s.preselected_choice="999";frame()
  clicks["Review home city change"]=true;clicks["Yes, confirm home choice"]=true;frame();check(#calls==0,"unreturned numeric ID became actionable")
  s.selection_revision="new-character";s.preselected_choice="0";s.revision="new-character-list";frame()
  clicks["Review home city change"]=true;clicks["Yes, confirm home choice"]=true;frame();check(#calls==0,"numeric selection survived context retirement")
  BASTION_UI.home_city=true;harness.width=1920;harness.height=1080;frame();harness.move(1600,900);frame();harness.width=1280;harness.height=720;frame();frame()
  local x,y,w,h=harness.rect();check(x>=0 and y>=0 and x+w<=1280 and y+h<=720,"restored home window stayed offscreen")
  local countBefore=#calls;absent=false;harness.width=440;harness.height=760;harness.images={};frame()
  local left,right=harness.childSizes["home-destination-left"],harness.childSizes["home-destination-right"]
  check(left and right and left.width==right.width and left.width>250,"narrow city choices did not receive the full browser width")
  check(table.concat(texts," "):find("Character: Human | Cleric",1,true),"narrow character summary retained the squeezed three-column table")
  tiles=0;for _,image in ipairs(harness.images)do if tostring(image.texture):find("A_StartingCityMap",1,true)then tiles=tiles+1 end end
  check(tiles==0 and #calls==countBefore,"compact city chooser drew the atlas or changed a home setting")
  harness.width=1000;harness.images={};frame();tiles=0
  for _,image in ipairs(harness.images)do if tostring(image.texture):find("A_StartingCityMap",1,true)then tiles=tiles+1 end end
  check(tiles==6 and #calls==countBefore,"returning to a wide window lost the authored city atlas or acted on layout")
  harness.close=true;frame();check(calls[#calls][1]=="close"and not BASTION_UI.home_city,"pack close did not retire home review")
  local before=stateReads;for _=1,120 do frame()end
  check(stateReads==before,"closed home city kept reading destinations")
  s.shown=true;s.revision="automatic-new-grant";s.entries[1].name="Fresh automatic destination";s.preselected_choice="0"
  local actions=#calls;frame()
  check(stateReads==before+1 and #calls==actions and table.concat(texts," "):find("Fresh automatic destination",1,true),
    "automatic native home prompt did not reopen with fresh destinations")
  active=false;before=stateReads;frame();check(not BASTION_UI.home_city and stateReads==before,"inactive home prompt fetched state or retained a review")
  active=true;s.shown=false;frame();check(stateReads==before,"reactivated hidden home city fetched state")
  harness.footer_layout(dofile("lib/citadel_received.lua"),{"Review home city change","Later","Refresh home city choices"},
    "Yes, confirm home choice","Cancel home choice")
  harness.split_layout(dofile("lib/citadel_received.lua"))
  harness.finish()
end
print("Home city UI PASS "..checks.." checks")
