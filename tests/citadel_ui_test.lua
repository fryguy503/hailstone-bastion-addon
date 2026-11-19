local root=ADDON_ROOT or(arg and arg[1])or"."
local setup=assert(loadfile(root.."/tests/received_tools_ui_harness.lua"))()
local checks=0
local function check(ok,why)checks=checks+1;assert(ok,why)end
for _,pack in ipairs({"modern","gamepad"})do
  local draw,clicks,changes,pick,calls,texts,options=nil,{},{},nil,{},{},0
  local clipboard,catalogCalls,sendCalls=nil,0,{}
  local integrityCalls,fullReads,visibilityReads=0,0,0
  local twistReads,twistCalls=0,{}
  local campReads,campCalls=0,{}
  local checkboxValues={}
  local camp={active=true,supported=true,enabled=false,armed=false,message="",error=""}
  local twist={active=true,running=false,queue="1 2 3",current=0,next=1,hold=0,settings={quiet=false,allow_nonbard=false},message="",error=""}
  local integrity={active=true,measured=false,sent=false,accepted=false,status="Waiting for server challenge",reason="",addon_version="0.1.0-preview.1",executable_bytes="123",addon_bytes="456"}
  local s={active=true,shown=false,open_revision="1",panel="tools",can_track=false,settings={right_click_sell=0,map_group=1,map_raid=1},message="",error=""}
  local catalog={commands={}}
  for i=1,205 do catalog.commands[i]={name=string.format("cmd%03d",i),help="Full command help "..i.." with MiXeD argument details.",access=i>200 and"GMAdmin"or"Player",category=i%2==0 and"Bastion"or"Server",shortcut=i==1 and"/one"or""}end
  local p={citadel=function(op,...)
      if op=="visibility"then visibilityReads=visibilityReads+1;return{active=s.active,shown=s.shown,open_revision=s.open_revision,panel=s.panel}end
      if op=="state"then fullReads=fullReads+1 end
      if op~="state"then calls[#calls+1]={op,...}end;return s
    end,
    commands=function(op,...)
      if op=="catalog"then catalogCalls=catalogCalls+1;return catalog end
      sendCalls[#sendCalls+1]={op,...};return{ok=true}
    end,
    firefell=function(op)if op=="refresh"then calls[#calls+1]={"firefell refresh"}end;return{active=true,available=true,ready=true,instance=10,version=250,name="Firefell: Trial"}end,
    integrity=function(op)check(op=="state","integrity UI attempted an active operation");integrityCalls=integrityCalls+1;return integrity end,
    twist=function(op,...)if op=="state"then twistReads=twistReads+1 else twistCalls[#twistCalls+1]={op,...}end;return twist end,
    fast_camp=function(op,...)if op=="state"then campReads=campReads+1 else campCalls[#campCalls+1]={op,...}end;return camp end}
  BASTION_UI={};eq={Plugin=function()return p end}
  ui={window=function(_,fn)draw=fn end,Begin=function()return true,true end,End=function()end,
    BeginChild=function()return true end,EndChild=function()end,SameLine=function()end,
    TextWrapped=function(v)texts[#texts+1]=v end,Button=function(label)return clicks[label]end,
    InputText=function(label,value)if changes[label]~=nil then return changes[label]end;return value end,
    Checkbox=function(label,value)checkboxValues[label]=value;if changes[label]~=nil then return changes[label],true end;return value,false end,
    BeginCombo=function()return false end,EndCombo=function()end,
    Selectable=function(label)
      if label:find("##command:",1,true)then options=options+1 end
      return pick and label:find(pick,1,true)~=nil
    end,
    SetClipboardText=function(value)clipboard=value end}
  local h=setup(root,pack,ui,eq,check)
  dofile(root.."/ui/"..pack.."/citadel.lua")
  local function frame()h.advance();options=0;texts={};draw();h.balanced();clicks={};changes={};pick=nil end
  local function panel(name)s.open_revision=tostring(tonumber(s.open_revision)+1);s.panel=name;frame()end
  for _=1,120 do frame()end
  check(fullReads==0 and visibilityReads==120 and #calls==0 and #sendCalls==0 and catalogCalls==0,"hidden hub requested full state or performed active work")
  check(twistReads==0 and #twistCalls==0,"hidden hub polled or started song twisting")
  check(campReads==0 and #campCalls==0,"hidden hub polled or requested fast camp")
  s.shown=true;frame();check(table.concat(texts," "):find("9 tools",1,true),"module inventory omitted")
  clicks["Encounter telegraphs"]=true;frame();check(calls[#calls][1]=="open"and calls[#calls][2]=="telegraphs","telegraph launcher missing")
  for _,name in ipairs({"telegraphs","gear_score","raidbars","citadel_follow"})do panel(name);check(BASTION_UI[name],"panel revision did not open module")end
  calls={};panel("settings")
  check(twistReads==1 and #twistCalls==0,"opening settings changed song twisting")
  check(campReads==1 and #campCalls==0,"opening settings requested fast camp")
  check(integrityCalls>0 and table.concat(texts," "):find("Waiting for server challenge",1,true),"passive integrity status missing from settings")
  integrity.measured=true;integrity.status="Report queued";frame();check(table.concat(texts," "):find("Measured; waiting to send",1,true),"measurement claimed a successful send")
  integrity.sent=true;integrity.status="Report sent";frame();local integrityText=table.concat(texts," ")
  check(integrityText:find("Report Sent",1,true)and integrityText:find("Pending",1,true)and not integrityText:find("Accepted",1,true),"report send was presented as server acceptance")
  integrity.accepted=true;integrity.status="Server accepted";frame();check(table.concat(texts," "):find("Accepted",1,true),"current server acknowledgement not shown")
  integrity.accepted=false;integrity.status="Server rejected";integrity.reason="This binary pair is not approved.";frame()
  check(table.concat(texts," "):find(integrity.reason,1,true)and #calls==0 and #sendCalls==0,"integrity status rendering sent an active request or lost server reason")
  integrity.native_protocol=false;integrity.measured=false;integrity.sent=false;integrity.reason=""
  integrity.legacy_measured=true;integrity.legacy_submitted=false;integrity.legacy_attempts=1
  integrity.legacy_addon_bytes="2529792";integrity.legacy_checksum="9007199254740993";integrity.addon_version="0.1.0-preview.4"
  integrity.status="Legacy DLL report queued";frame();integrityText=table.concat(texts," ")
  check(integrityText:find("Legacy report Measured; waiting to send",1,true)and integrityText:find("Unconfirmed (no acknowledgement)",1,true)
    and not integrityText:find("Accepted",1,true),"legacy queued report claimed server acceptance or lost queue status")
  integrity.status="Report unavailable";integrity.legacy_attempts=3;integrity.reason="Legacy send queue unavailable.";frame();integrityText=table.concat(texts," ")
  check(integrityText:find("Legacy report Report unavailable",1,true)and integrityText:find(integrity.reason,1,true)
    and not integrityText:find("Measured; waiting to send",1,true),"exhausted legacy retries were displayed as still waiting to send")
  integrity.status="Legacy DLL report queued";integrity.reason=""
  clicks["Binary details"]=true;frame();integrityText=table.concat(texts," ")
  check(integrityText:find("bastion.dll 2529792 bytes",1,true)and integrityText:find("FNV63 checksum 9007199254740993",1,true)
    and integrityText:find("Version 0.1.0-preview.4",1,true)and not integrityText:find("EXE / addon",1,true),
    "legacy binary details lost exact DLL identity or claimed executable measurement")
  integrity.legacy_submitted=true;integrity.status="Legacy DLL report submitted";frame();integrityText=table.concat(texts," ")
  check(integrityText:find("Legacy report Submitted",1,true)and integrityText:find("Unconfirmed (no acknowledgement)",1,true)
    and not integrityText:find("Accepted",1,true),"legacy local submission was presented as server approval")
  integrity.accepted=true;frame();check(not table.concat(texts," "):find("Accepted",1,true),"legacy report inherited an unrelated acceptance flag")
  integrity.requires_reconnect=true;integrity.status="Reconnect required"
  integrity.reason="Bastion loaded after the world login checks. Return to the login screen and reconnect to submit the DLL check."
  frame();integrityText=table.concat(texts," ")
  check(integrityText:find("Report Reconnect required",1,true)and integrityText:find("Server Not submitted",1,true)
    and integrityText:find(integrity.reason,1,true)and not integrityText:find("Legacy report Submitted",1,true)
    and not integrityText:find("Accepted",1,true)and not integrityText:find("FNV63 checksum",1,true),
    "late approval or a zoning report claimed a login submission or stale server acceptance")
  integrity.requires_reconnect=nil
  integrity.native_protocol=true;integrity.measured=true;integrity.sent=true;integrity.accepted=false
  integrity.status="Server rejected";integrity.reason="Native pair rejected; no legacy fallback.";frame();integrityText=table.concat(texts," ")
  check(integrityText:find("Report Sent",1,true)and integrityText:find("Server rejected",1,true)and integrityText:find(integrity.reason,1,true)
    and integrityText:find("EXE / addon 123 / 456 bytes",1,true)and not integrityText:find("Legacy report",1,true)
    and not integrityText:find("FNV63 checksum",1,true),"valid v3 protocol did not supersede a prior legacy report")
  integrity.measured=false;integrity.sent=false;integrity.status="Unsupported";frame();integrityText=table.concat(texts," ")
  check(integrityText:find("Unsupported",1,true)and not integrityText:find("Legacy report",1,true)
    and not integrityText:find("FNV63 checksum",1,true),"unsupported v3 challenge displayed a legacy fallback")
  integrity.measured=true;integrity.sent=true
  integrity.accepted=true;integrity.status="Server accepted";frame()
  check(table.concat(texts," "):find("Accepted",1,true),"v3 acknowledgement lost acceptance with legacy history")
  integrity.native_protocol=false;integrity.measured=false;integrity.sent=false;integrity.accepted=false
  integrity.legacy_measured=false;integrity.legacy_submitted=false;integrity.status="Report unavailable";integrity.reason="DLL measurement unavailable.";frame();integrityText=table.concat(texts," ")
  check(integrityText:find("Report unavailable",1,true)and integrityText:find(integrity.reason,1,true)and not integrityText:find("FNV63 checksum",1,true)
    and not integrityText:find("Legacy report",1,true),"failed DLL measurement fabricated legacy binary details or submission")
  integrity.native_protocol=nil;integrity.legacy_measured=nil;integrity.legacy_submitted=nil;integrity.legacy_attempts=nil
  integrity.legacy_addon_bytes=nil;integrity.legacy_checksum=nil;integrity.measured=false;integrity.sent=false;integrity.accepted=false
  integrity.status="Waiting for server challenge";integrity.reason="";frame();integrityText=table.concat(texts," ")
  check(integrityText:find("Waiting for server challenge",1,true)and not integrityText:find("Legacy report",1,true)
    and not integrityText:find("FNV63 checksum",1,true)and #calls==0 and #sendCalls==0,
    "missing legacy fields broke old-host display or passive integrity rendering performed an action")
  changes["NPCs"]=true;frame();check(#calls==0,"nontracker changed NPC visibility")
  changes["Group members"]=false;frame()
  check(calls[#calls][1]=="set"and calls[#calls][2]=="map_group"and calls[#calls][3]==0 and s.settings.map_group==1,
    "nontracker group filter was unavailable or mutated the observed settings")
  changes["Right-click selling"]=true;frame();check(calls[#calls][1]=="set"and calls[#calls][2]=="right_click_sell"and calls[#calls][3]==1,"optional selling control missing")
  check(s.settings.right_click_sell==0,"drawing altered host preference directly")
  s.can_track=true;changes["NPCs"]=false;frame();check(calls[#calls][2]=="map_mobs"and calls[#calls][3]==0,"tracker filter control missing")
  changes["Quiet twist messages"]=true;frame()
  check(twistCalls[#twistCalls][1]=="set"and twistCalls[#twistCalls][2]=="quiet"and twistCalls[#twistCalls][3]==1 and twist.settings.quiet==false,"twist quiet preference mutated observed state")
  changes["Allow twisting on other classes"]=true;frame()
  check(twistCalls[#twistCalls][2]=="allow_nonbard"and twistCalls[#twistCalls][3]==1,"twist class preference missing")
  clicks["Resume twist"]=true;frame();check(twistCalls[#twistCalls][1]=="command"and twistCalls[#twistCalls][2]=="start","explicit twist resume missing")
  twist.running=true;clicks["Stop twist"]=true;frame();check(twistCalls[#twistCalls][2]=="stop","explicit twist stop missing")
  local twistActions=#twistCalls;frame();check(#twistCalls==twistActions,"drawing twist controls started an action")
  twist.settings.quiet=true;twist.settings.allow_nonbard=true;frame()
  check(checkboxValues["Quiet twist messages"]and checkboxValues["Allow twisting on other classes"]and #twistCalls==twistActions,"boolean twist settings were drawn unchecked or triggered an action")
  changes["Fast camp when offered"]=true;frame()
  check(campCalls[#campCalls][1]=="set"and campCalls[#campCalls][2]=="enabled"and campCalls[#campCalls][3]==1 and not camp.enabled,"camp preference mutated observed state")
  camp.enabled=true;local beforeCampPaint=#campCalls;frame()
  check(checkboxValues["Fast camp when offered"]and #campCalls==beforeCampPaint,"boolean camp preference was drawn unchecked or triggered an action")
  clicks["Request fast camp"]=true;frame();check(campCalls[#campCalls][1]=="start","explicit fast camp request missing")
  camp.armed=true;clicks["Use normal countdown"]=true;frame();check(campCalls[#campCalls][1]=="cancel","explicit normal-countdown fallback missing")
  camp.armed=false;camp.supported=false;local campActions=#campCalls
  changes["Fast camp when offered"]=true;clicks["Request fast camp"]=true;frame()
  check(#campCalls==campActions,"unsupported host changed or started fast camp")
  check(table.concat(texts," "):find("Requires the matching Hailstone update",1,true),"unavailable fast camp omitted its client requirement")
  panel("commands");check(options==16 and catalogCalls==1 and #sendCalls==0,"command catalog not paginated/passive")
  check(table.concat(texts," "):find("205 server",1,true),"complete server catalog count missing")
  changes["Search commands and help"]="cmd205";frame();check(options==0,"staff command exposed by default")
  changes["Include staff commands"]=true;frame();check(options==1,"staff toggle omitted registered command")
  changes["Search commands and help"]="cmd001";pick="##command:cmd001";frame()
  check(table.concat(texts," "):find("Full command help 1 with MiXeD argument details.",1,true),"complete command help missing")
  changes["Command arguments"]='MiXeD "Value Case"';frame();check(#sendCalls==0,"editing arguments executed command")
  clicks["Copy command"]=true;frame();check(clipboard=='#cmd001 MiXeD "Value Case"'and #sendCalls==0,"copy changed arguments or sent command")
  clicks["Prepare command"]=true;frame();check(#sendCalls==0,"prepare executed command")
  clicks["Send command"]=true;frame();check(#sendCalls==1 and sendCalls[1][1]=="send"and sendCalls[1][2]=="cmd001"and sendCalls[1][3]=='MiXeD "Value Case"',"explicit send lost exact arguments")
  clicks["Prepare command"]=true;frame();changes["Command arguments"]="Changed";clicks["Send command"]=true;frame();check(#sendCalls==1,"changed arguments reused prepared command")
  changes["Command arguments"]="bad\nsecond";clicks["Prepare command"]=true;frame();clicks["Send command"]=true;frame();check(#sendCalls==1,"multiline command prepared")
  changes["Search commands and help"]="/petgear";pick="##command:/petgear";frame();check(options>=1,"local command reference missing")
  clicks["Open command tool"]=true;frame();check(calls[#calls][1]=="open"and calls[#calls][2]=="petinventory"and #sendCalls==1,"local tool sent server command")
  changes["Search commands and help"]="";frame();check(options==16,"full list exceeded page budget")
  for _=1,14 do clicks["Next commands"]=true;frame();frame();check(options<=16,"command page exceeded bound")end
  check(options==1,"last command page omitted records")
  check(catalogCalls==1,"drawing repeatedly fetched unchanged catalog")
  calls={};panel("firefell");check(table.concat(texts," "):find("Firefell: Trial",1,true)and #calls==0,"Firefell context not passive")
  clicks["Refresh Firefell context"]=true;frame();check(calls[#calls][1]=="firefell refresh","manual Firefell refresh missing")
  h.width=1000;h.height=600;panel("commands");h.width=1920;h.height=1080;frame()
  s.active=false;frame();check(#texts==0 and #sendCalls==1,"unsupported peer displayed/sent commands")
  s.active=true;s.shown=true;panel("tools");h.close=true;frame();check(calls[#calls][1]=="close","hub close not forwarded")
  h.finish()
end
print("Bastion hub UI PASS "..checks.." checks")
