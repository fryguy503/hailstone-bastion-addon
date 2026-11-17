local root=ADDON_ROOT or (arg and arg[1]) or "."
local setup=assert(loadfile(root.."/tests/received_tools_ui_harness.lua"))()
local checks=0
local function check(ok,why) checks=checks+1;if not ok then error(why,2) end end
for _,pack in ipairs({"modern","gamepad"}) do
  local draw,clicks,selection,calls,texts=nil,{},nil,{},{}
  local active,visibilityReads,stateReads=true,0,0
  local s={available=true,shown=false,pending=false,fresh=true,revision="18446744073709551614",message="Received",entries={
    {dz_id="900",uuid="01234567-89ab-cdef-0123-456789abcdef",name="Expedition",leader="Peer",zone_id=9,zone_version=3,instance_id=1,members=2,maximum=6,remaining_seconds="599",eligibility="Ready",can_rejoin=true}}}
  BASTION_UI={}
  local p={saved_expeditions=function(op,...)
    if op=="visibility"then visibilityReads=visibilityReads+1;return{active=active,shown=s.shown}end
    if op=="state"then stateReads=stateReads+1 else calls[#calls+1]={op,...}end
    if op=="close"then s.shown=false end
    return s
  end}
  eq={Plugin=function(name) check(name=="bastion","wrong plugin");return p end}
  ui={Cond={FirstUseEver=1},window=function(_,fn)draw=fn end,SetNextWindowSize=function()end,Begin=function()return true,true end,End=function()end,
    TextWrapped=function(v)texts[#texts+1]=v end,BeginChild=function()return true end,EndChild=function()end,SameLine=function()end,
    Button=function(label)local hit=clicks[label];clicks[label]=nil;return hit end,
    Selectable=function(label)if selection and label:find(selection,1,true) then selection=nil;return true end return false end}
  local harness=setup(root,pack,ui,eq,check)
  dofile(root.."/ui/"..pack.."/saved_expeditions.lua")
  local function frame() harness.advance();draw();harness.balanced();clicks={};selection=nil end
  for _=1,120 do frame()end
  check(visibilityReads==120 and stateReads==0 and #calls==0,"hidden window requested the saved expedition list")
  BASTION_UI.saved_expeditions=true;clicks["Review rejoin"]=true;clicks["Yes, rejoin"]=true;frame();check(#calls==0,"missing expedition was actionable")
  selection="Expedition";frame();check(#calls==0,"selection sent action")
  clicks["Review rejoin"]=true;frame();check(#calls==0,"review sent action")
  s.revision="18446744073709551615";clicks["Yes, rejoin"]=true;frame();check(#calls==0,"stale review sent action")
  selection="Expedition";clicks["Review rejoin"]=true;frame();clicks["Yes, rejoin"]=true;frame()
  check(#calls==1 and calls[1][1]=="resume" and calls[1][2]==s.revision and calls[1][3]=="900" and calls[1][4]==s.entries[1].uuid,"exact opaque request tuple lost")
  calls={};s.pending=true;clicks["Review rejoin"]=true;clicks["Refresh saved expeditions"]=true;frame();check(#calls==0,"pending request allowed duplicate action")
  s.pending=false;s.entries[1].can_rejoin=false;clicks["Review rejoin"]=true;clicks["Yes, rejoin"]=true;frame();check(#calls==0,"ineligible row actionable")
  clicks["Refresh saved expeditions"]=true;frame();check(#calls==1 and calls[1][1]=="refresh","manual refresh unavailable")
  check(table.concat(texts," "):find("usual NPC or entrance",1,true)~=nil,"entrance guidance missing")
  -- Equal names never conflate distinct saved identities. A selection remains
  -- read-only and a replaced UUID invalidates the inline confirmation.
  calls={};s.entries[1].can_rejoin=true
  local original=s.entries[1];local other={};for k,v in pairs(original)do other[k]=v end
  other.dz_id="901";other.uuid="fedcba98-7654-3210-fedc-ba9876543210";s.entries[2]=other
  selection="##saved901";frame();clicks["Review rejoin"]=true;frame();check(#calls==0,"review sent same-name rejoin")
  other.uuid="11111111-2222-3333-4444-555555555555";clicks["Yes, rejoin"]=true;frame();check(#calls==0,"replacement UUID inherited confirmation")
  harness.width=1920;harness.height=1080;frame();harness.width=1280;harness.height=720;frame()
  harness.close=true;frame();check(calls[#calls][1]=="close"and not BASTION_UI.saved_expeditions,"pack close did not retire review/window")
  local before=stateReads;for _=1,120 do frame()end
  check(stateReads==before,"closed expeditions kept reading rows")
  s.shown=true;s.revision="fresh-hidden-expeditions";s.entries[1].name="New hidden expedition"
  local actions=#calls;frame();check(stateReads==before+1 and #calls==actions,"native DZ reopening missed fresh state or sent an action")
  active=false;before=stateReads;frame();check(not BASTION_UI.saved_expeditions and stateReads==before,"inactive expeditions fetched state or retained review visibility")
  active=true;s.shown=false;frame();check(stateReads==before,"reactivated hidden expeditions fetched state")
  harness.footer_layout(dofile("lib/citadel_received.lua"),{"Review rejoin","Refresh saved expeditions","Close expeditions"},
    "Yes, rejoin","Cancel rejoin")
  harness.split_layout(dofile("lib/citadel_received.lua"))
  harness.finish()
end
print("Saved expeditions UI PASS "..checks.." checks")
