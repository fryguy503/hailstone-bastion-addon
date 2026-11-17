local root=ADDON_ROOT or(arg and arg[1])or"."
local checks=0
local function check(value,why)checks=checks+1;assert(value,why)end
local Host=dofile(root.."/tests/raidbars_ui_host.lua")
local definitions={
 {"map_group","Group members"},{"map_raid","Raid names"},{"map_mobs","NPCs"},
 {"map_merchants","Merchants"},{"map_players","Other players"},{"map_corpses","Corpses"},
}
local function proxy(value)
 return setmetatable({},{__index=function(_,key)
  local item=value[key]
  -- Source-bound functions and namespace proxies need not retain identity across reads.
  if type(item)=="function"then return function(...)return item(...)end end
  if type(item)=="table"then return proxy(item)end
  return item
 end,__newindex=function()error("Map filters mutated the host facade")end,
 __pairs=function()error("Map filters enumerated the host facade")end,__metatable=false})
end
local function clone(value)
 if type(value)~="table"then return value end
 local out={};for key,item in pairs(value)do out[key]=clone(item)end;return out
end

for _,pack in ipairs({"modern","gamepad"})do
 local state={active=true,shown=false,can_track=false,settings={map_group=1,map_raid=1,map_mobs=1,map_merchants=0,map_players=1,map_corpses=0}}
 local policy={context="context:18446744073709551615",revision="revision:9007199254740993",custom_spawns=true,stock_npcs=false,npc_find=false,name="Bastion"}
 local calls,reads,policyReads={},0,0
 local plugin={citadel=function(operation,...)
  if operation=="state"then reads=reads+1;return state end
  check(operation=="set","Map filters used an action instead of the settings service")
  calls[#calls+1]={...}
  return{ok=1}
 end}
 local current=plugin
 local h=Host.new(root,pack,check,plugin)
 local original=hs
 local registry,owned={},{}
 local registrations,removals=0,0
 local failRegistration,throwRegistration=false,false
 local throwLiveness,throwRemoval,throwPolicy=false,false,false
 local extensionAvailable,mapAvailable,permission=true,true,true
 local rendered,disabled,changes,afterRow={},{},{},nil
 local service={}
 service.RegisterSection=function(window,section,placement,id,callback,options)
  registrations=registrations+1
  check(window=="MapViewWnd"and section=="controls"and placement=="after"and id=="bastion.map.filters",
   "Map rows registered outside the stock map controls slot")
  check(type(callback)=="function"and type(options)=="table"and options.priority==0,
   "Map registration omitted its draw callback or priority")
  if throwRegistration then error("Simulated lost UI registration permission")end
  if failRegistration then return nil end
  local handle={};owned[handle]=true
  registry[handle]={window=window,section=section,placement=placement,id=id,callback=callback,options=options}
  return handle
 end
 service.Remove=function(handle)
  check(owned[handle],"Map filters removed another owner's opaque handle")
  if throwRemoval then error("Owner already retired")end
  if not registry[handle]then return false end
  registry[handle]=nil;removals=removals+1;return true
 end
 service.IsRegistered=function(handle)
  if throwLiveness then error("Registration context changed")end
  return registry[handle]~=nil
 end
 service.ListSections=function()
  local metadata={}
  for handle,entry in pairs(registry)do
   metadata[#metadata+1]={handle=handle,id=entry.id,window=entry.window,section=entry.section,enabled=permission}
  end
  return metadata
 end
 service.DrawSection=function(window,section,placement,context)
  local count=0
  if not permission then return count end
  for _,entry in pairs(registry)do
   if entry.window==window and entry.section==section and entry.placement==placement then
    entry.callback(window,section,placement,context);count=count+1
   end
  end
  return count
 end
 local function facade()
  hs=proxy({UI={version={major=1,minor=4},plugins={Get=function(name)
   check(name=="bastion","Map filters resolved another addon")
   original.UI.plugins.Get(name)
   return current
  end},extensions=extensionAvailable and service or nil,map=mapAvailable and{Policy=function()
   policyReads=policyReads+1
   if throwPolicy then error("Map policy is no longer readable")end
   return clone(policy)
  end}or nil}})
 end
 -- No addon-owned popup/window: the core map presenter owns its controls panel.
 for _,name in ipairs({"window","Begin","End","OpenPopup","BeginPopup","EndPopup"})do
  ui[name]=function()error("Map filters created an implicit window or popup: "..name)end
 end
 ui.BeginDisabled=function(value)disabled[#disabled+1]=value==true or disabled[#disabled]==true end
 ui.EndDisabled=function()check(#disabled>0,"Disabled scope underflow");table.remove(disabled)end
 ui.Checkbox=function(label,value)
  rendered[#rendered+1]={label=label,value=value,disabled=disabled[#disabled]==true}
  local requested=changes[label];changes[label]=nil
  if afterRow then afterRow(label)end
  -- Deliberately return attempted changes even inside disabled scopes: addon guards must also deny them.
  if requested~=nil then return requested,true end
  return value,false
 end
 local function balanced()
  check(#disabled==0 and h.styles==0 and h.vars==0 and h.fonts==0 and h.children==0 and h.tables==0,
   "Map callback leaked native UI/style scopes")
 end
 local function resetPaint()rendered={};changes={};h.texts={};afterRow=nil end
 local function context()
  return{window="MapViewWnd",section="controls",view="zone",zone="qeynos2",shown_zone="qeynos2",own_zone=true,
   revision="opaque presenter revision",policy=clone(policy)}
 end
 local function count()local n=0;for _ in pairs(registry)do n=n+1 end;return n end
 local function registration()
  for handle,entry in pairs(registry)do return handle,entry end
  error("Map section is not registered")
 end
 local M
 local function sync()M.sync(current,state);balanced()end
 local function draw(value)
  service.DrawSection("MapViewWnd","controls","after",value or context());balanced()
 end
 local function layout(tracking)
  check(#rendered==#definitions,"Map controls omitted or duplicated a shared filter row")
  for index,definition in ipairs(definitions)do
   local row=rendered[index]
   check(row.label==definition[2]and row.value==(state.settings[definition[1]]==1),
    "A filter row lost its label, order or current preference")
   check(row.disabled==(index>2 and not tracking),"Tracking admission was applied to the wrong map row")
  end
 end
 facade();M=dofile("lib/map_filters.lua")
 check(count()==0 and #calls==0 and reads==0,"Loading map filters performed registration or settings work")
 sync();local handle,entry=registration()
 check(count()==1 and registrations==1 and reads==0 and policyReads==0 and #calls==0,
  "Passive sync performed native state/actions or depended on a visible hub")
 for _=1,3 do sync();service.ListSections()end
 check(registrations==1 and #calls==0,"Fresh API method/proxy wrappers churned handles or ran actions on metadata paint")
 draw();layout(false)
 check(h.texts["Bastion filters"]and h.texts["Trained Tracking required."]and #calls==0 and reads==1,
  "Map section lost its heading/admission hint or wrote settings on paint")

 local beforeSettings=clone(state.settings)
 resetPaint();changes["NPCs"]=false;changes["Merchants"]=true;changes["Other players"]=false;changes["Corpses"]=true
 draw();check(#calls==0,"A nontracker changed a disabled map filter")
 resetPaint();changes["Group members"]=false;draw()
 check(#calls==1 and calls[1][1]=="map_group"and calls[1][2]==0,"Group display did not toggle independently without Tracking")
 resetPaint();changes["Raid names"]=false;draw()
 check(#calls==2 and calls[2][1]=="map_raid"and calls[2][2]==0,"Raid names did not toggle independently without Tracking")
 for key,value in pairs(beforeSettings)do check(state.settings[key]==value,"Drawing edited saved preferences directly")end
 state.can_track=true;calls={};resetPaint();draw();layout(true)
 for index,definition in ipairs(definitions)do
  resetPaint();local wanted=state.settings[definition[1]]~=1;changes[definition[2]]=wanted;draw()
  local call=calls[#calls]
  check(#calls==index and call[1]==definition[1]and call[2]==(wanted and 1 or 0),
   "An explicit filter edit used the wrong setting, integer value or number of writes")
 end
 for key,value in pairs(beforeSettings)do check(state.settings[key]==value,"Explicit edits mutated the received state table")end

 -- The hub and stock map share the same authored rows, not copied checkbox definitions.
 local style={note=function()end,tooltip=function()end}
 calls={};resetPaint();M.draw_settings(plugin,state,style);balanced();layout(true)
 check(#calls==0,"The shared hub settings rows saved on paint")
 resetPaint();changes["Other players"]=false;M.draw_settings(plugin,state,style);balanced()
 check(#calls==1 and calls[1][1]=="map_players"and calls[1][2]==0,"Shared hub row lost the exact settings service")
 calls={};local applied={};resetPaint();changes["Corpses"]=true
 M.draw_settings(plugin,state,style,function(key,value)applied[#applied+1]={key,value}end);balanced()
 check(#calls==0 and #applied==1 and applied[1][1]=="map_corpses"and applied[1][2]==1,
  "Shared rows bypassed the map's live apply guard")

 -- Changes occurring during a widget must be checked again before every settings request.
 calls={};resetPaint();changes["NPCs"]=false
 afterRow=function(label)if label=="NPCs"then state={active=true,can_track=false,settings=clone(beforeSettings)}end end
 draw();check(#calls==0,"Tracking revoked after drawing the checkbox still allowed its settings write")
 state.can_track=true;resetPaint();changes["Group members"]=false
 afterRow=function(label)if label=="Group members"then state.active=false end end
 draw();check(#calls==0,"Peer/context retirement during a widget still allowed its settings write")
 state.active=true;resetPaint();changes["Group members"]=false
 afterRow=function(label)if label=="Group members"then policy.context="new connection context"end end
 local oldContext=context();draw(oldContext);check(#calls==0,"A changed connection context accepted a stale filter edit")
 resetPaint();changes["Group members"]=false
 afterRow=function(label)if label=="Group members"then policy.revision="new owner revision"end end
 oldContext=context();draw(oldContext);check(#calls==0,"A changed ownership revision accepted a stale filter edit")
 resetPaint();local stale=context();policy.context="next zone context";draw(stale)
 check(#rendered==0 and #calls==0,"Stale policy context displayed controls or changed settings")
 resetPaint();stale=context();policy.revision="same-name replacement provider";draw(stale)
 check(#rendered==0 and #calls==0,"Stale policy revision displayed controls or changed settings")
 for _,bad in ipairs({{}, {policy={}}, {policy={context="",revision=policy.revision}},
  {policy={context=policy.context,revision=""}}, {policy={context=123,revision=policy.revision}},
  {policy={context=policy.context,revision=123}}})do
  resetPaint();draw(bad);check(#rendered==0 and #calls==0,"Malformed or missing opaque map context remained interactive")
 end
 for _,target in ipairs({{"OtherWindow","controls","after"},{"MapViewWnd","other","after"},{"MapViewWnd","controls","before"}})do
  resetPaint();entry.callback(target[1],target[2],target[3],context());balanced()
  check(#rendered==0 and #calls==0,"A callback from another map slot remained interactive")
 end

 -- New hosts add an opaque provider-source lifecycle token; older hosts omit it on both sides.
 resetPaint();draw();layout(true)
 check(#calls==0,"Missing optional source on both sides broke older-host presentation")
 policy.source="source:18446744073709551615"
 resetPaint();draw();layout(true)
 check(#calls==0,"Matching optional source wrote settings on paint")
 resetPaint();changes["Raid names"]=false;draw()
 check(#calls==1 and calls[1][1]=="map_raid"and calls[1][2]==0,"Matching optional source denied an explicit filter change")
 calls={}
 local oldSource=context();local sameContext,sameRevision=policy.context,policy.revision
 policy.source="replacement source with identical policy content"
 resetPaint();draw(oldSource)
 check(#rendered==0 and #calls==0 and policy.context==sameContext and policy.revision==sameRevision,
  "Changed source with the same context/revision displayed stale controls")
 for _,pair in ipairs({{nil,"current source"},{"expected source",nil},{"",""},{123,123},{false,false},
  {"expected source",123},{123,"123"},{"expected source",""},{"expected source",false}})do
  policy.source=pair[2];local mismatch=context();mismatch.policy.source=pair[1]
  resetPaint();changes["Group members"]=false;draw(mismatch)
  check(#rendered==0 and #calls==0,"Missing, empty or non-string optional source stayed interactive")
 end
 policy.source="before widget";local changingSource=context()
 resetPaint();changes["Group members"]=false
 afterRow=function(label)if label=="Group members"then policy.source="after widget"end end
 draw(changingSource)
 check(#calls==0 and policy.context==sameContext and policy.revision==sameRevision,
  "Provider source changed during a widget but its stale edit was saved")
 policy.source="source removed during widget";changingSource=context()
 resetPaint();changes["Raid names"]=false
 afterRow=function(label)if label=="Raid names"then policy.source=nil end end
 draw(changingSource);check(#calls==0,"Removing optional source during a widget preserved a stale edit")
 changingSource=context();resetPaint();changes["Group members"]=false
 afterRow=function(label)if label=="Group members"then policy.source="source added during widget"end end
 draw(changingSource);check(#calls==0,"Adding optional source during a widget preserved a stale edit")
 policy.source=nil;resetPaint();draw();layout(true)
 check(#calls==0,"Returning to an older host without source retained stale filtering state")

 resetPaint();throwPolicy=true;draw();check(#rendered==0 and #calls==0,"Unreadable current policy displayed stale controls");throwPolicy=false
 resetPaint();current=nil;draw();check(#rendered==0 and #calls==0,"Map draw cached an unloaded native plugin")
 current=plugin;state.active=false;resetPaint();draw();check(#rendered==0 and #calls==0,"Inactive peer displayed map settings")
 state.active=true
 local replacementCalls={}
 local replacement={citadel=function(operation,...)
  if operation=="state"then return state end
  check(operation=="set","Replacement plugin received a raw action");replacementCalls[#replacementCalls+1]={...}
 end}
 resetPaint();changes["Raid names"]=false
 afterRow=function(label)if label=="Raid names"then current=replacement end end
 draw();check(#calls==0 and #replacementCalls==1 and replacementCalls[1][1]=="map_raid",
  "An edited row used a plugin captured before the widget")
 current=plugin;resetPaint()

 local registrationsBefore=registrations
 permission=false;check(service.IsRegistered(handle),"Fixture confused enabled state with owner liveness")
 sync();service.ListSections();draw()
 check(registrations==registrationsBefore and #rendered==0 and #calls==0,
  "Temporary host permission loss churned registrations or invoked a disabled section")
 permission=true;resetPaint();draw();layout(true)
 local retiredCallback=entry.callback;registry[handle]=nil
 resetPaint();changes["Group members"]=false;retiredCallback("MapViewWnd","controls","after",context());balanced()
 check(#rendered==0 and #calls==0,"A silently retired host handle remained interactive before the next sync")
 sync();check(count()==1 and registrations==registrationsBefore+1,"Silently retired source-bound map handle was not repaired")
 resetPaint();changes["Group members"]=false;retiredCallback("MapViewWnd","controls","after",context());balanced()
 check(#rendered==0 and #calls==0,"An old source-generation closure remained interactive after handle repair")
 handle,entry=registration();registrationsBefore=registrations
 for _=1,3 do sync()end
 check(registrations==registrationsBefore,"Fresh API function/proxy identity retired a live map handle")
 throwLiveness=true;sync();check(count()==1 and registrations==registrationsBefore+1,"A failed liveness check left an unmanaged old handle")
 throwLiveness=false

 local function unavailable(change,restore,why)
  local _,old=registration();local saved=old.callback
  change();facade();sync();check(count()==0,why)
  resetPaint();changes["Group members"]=false;saved("MapViewWnd","controls","after",context());balanced()
  check(#rendered==0 and #calls==0,"A retired map closure saved a preference")
  restore();facade();sync();check(count()==1,"Map registration did not recover after host/context restoration")
 end
 unavailable(function()current=nil end,function()current=plugin end,"Unloaded native plugin retained its map section")
 unavailable(function()state.active=false end,function()state.active=true end,"Retired Bastion peer retained its map section")
 unavailable(function()extensionAvailable=false end,function()extensionAvailable=true end,"Missing extensions service retained a map section")
 unavailable(function()mapAvailable=false end,function()mapAvailable=true end,"Missing optional map Policy API retained a map section")
 local saved=service.IsRegistered
 unavailable(function()service.IsRegistered=nil end,function()service.IsRegistered=saved end,"Missing handle liveness support exposed unmanaged map controls")
 saved=service.Remove
 unavailable(function()service.Remove=nil end,function()service.Remove=saved end,"Registration without an owned remove method leaked a map section")
 saved=plugin.citadel
 unavailable(function()plugin.citadel=nil end,function()plugin.citadel=saved end,"Missing live settings service retained map controls")
 M.remove();check(count()==0,"Explicit lifecycle cleanup retained map controls")
 failRegistration=true;sync();check(count()==0,"Failed registration published a partial map section");failRegistration=false
 throwRegistration=true;sync();check(count()==0,"Revoked registration permission escaped or leaked controls");throwRegistration=false
 sync();check(count()==1,"Registration failed to recover after permission restoration")
 local _,last=registration();local lastCallback=last.callback
 registry={};throwRemoval=true;M.remove();throwRemoval=false
 resetPaint();changes["Group members"]=false;lastCallback("MapViewWnd","controls","after",context());balanced()
 check(#rendered==0 and #calls==0,"Failed host cleanup left the retired generation actionable")
 M.remove();check(count()==0 and #calls==0,"Repeated removal sent settings or an action")
 check(removals>0 and reads>0 and policyReads>0,"Lifecycle/live policy coverage was bypassed")
 h.restore()
end
local function source(pack)
 local file=assert(io.open(root.."/ui/"..pack.."/lib/map_filters.lua","rb"));local value=file:read("a");file:close();return value
end
check(source("modern")==source("gamepad"),"Shared map filter helpers differ between packs")
print("Map filter extension UI: "..checks.." checks passed")
