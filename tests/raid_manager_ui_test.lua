local root=ADDON_ROOT or(arg and arg[1])or"."
local checks=0
local function check(value,why)checks=checks+1;assert(value,why)end
local Host=dofile(root.."/tests/raidbars_ui_host.lua")
local function proxy(value)
 return setmetatable({},{__index=value,__newindex=function()error("Extension API proxy was mutated")end,
  __pairs=function()error("Extension API proxy was enumerated")end,__metatable=false})
end
local function freshMethods(value)
 return setmetatable({},{__index=function(_,key)
  local method=value[key]
  if type(method)=="function"then return function(...)return method(...)end end
  return method
 end,__newindex=function()error("Extension API proxy was mutated")end,
 __pairs=function()error("Extension API proxy was enumerated")end,__metatable=false})
end
for _,pack in ipairs({"modern","gamepad"})do
 local state={available=1,in_zone=1,in_raid=1,manager_supported=1,error="",rows={},settings={enabled=0,manage=0}}
 local native,stateReads={},0
 local response={ok=1,message="Request sent. Check server chat for confirmation."}
 local plugin={raidbars=function(operation)
  check(operation=="status","Stock actions requested a full Raid Bars snapshot");stateReads=stateReads+1;return state
 end,raid_manager_action=function(...)
  native[#native+1]={...};return response
 end}
 local h=Host.new(root,pack,check,plugin)
 local original=hs
 local current=plugin
 local registry,owned,removed,registrations={},{},0,0
 local failId,throwId=nil,nil
 local service={}
 service.RegisterAction=function(window,section,id,label,options,callback)
  registrations=registrations+1
  check(window=="RaidWindow"and section=="member_actions","Stock action registered outside the actual Raid slot")
  check(options.requiredCaps[1]=="chat"and #options.requiredCaps==1,"Stock action lost current chat admission")
  check(type(callback)=="function","Stock action registered a request result instead of a callback")
  if id==throwId then error("Simulated revoked registration permission")end
  if id==failId then return nil end
  local handle={}
  owned[handle]=true
  registry[handle]={window=window,section=section,id=id,label=label,options=options,callback=callback}
  return handle
 end
 service.Remove=function(handle)
  check(owned[handle],"Stock action removed a handle belonging to another owner")
  if not registry[handle]then return false end
  registry[handle]=nil;removed=removed+1
  return true
 end
 service.IsRegistered=function(handle)return registry[handle]~=nil end
 service.ListActions=function(window,section)
  local metadata={}
  for handle,action in pairs(registry)do
   if action.window==window and action.section==section then
    metadata[#metadata+1]={handle=handle,id=action.id,label=action.label,options=action.options}
   end
  end
  return metadata
 end
 service.InvokeAction=function(handle,phase,context,token)
  local action=registry[handle]
  if not action then return{ok=false,message="This action is no longer registered."}end
  return action.callback(action.window,action.id,phase,context,token)
 end
 local extension=service
 local function facade()
  hs=proxy({UI=proxy({plugins=proxy({Get=function(name)
   check(name=="bastion","Stock callback looked up another addon")
   -- Keep the existing host mock's canonical-read accounting active too.
   original.UI.plugins.Get(name)
   return current
  end}),extensions=extension and freshMethods(extension)or nil})})
 end
 local function actions()
  local out={};for _,metadata in ipairs(service.ListActions("RaidWindow","member_actions"))do out[metadata.id]=metadata end;return out
 end
 local function count()local n=0;for _ in pairs(registry)do n=n+1 end;return n end
 local function frame()h.frame("Bastion Raid Bars")end
 facade();h.load("raidbars.lua")
 check(registrations==0 and #native==0,"Loading stock registration ran an action")
 frame();local a=actions()
 check(count()==3 and registrations==3 and stateReads==1 and #native==0,
  "Stock controls required the overlay, duplicated state sampling or sent a request on paint")
 check(a["bastion.manager.list"].label=="List managers"and a["bastion.manager.list"].options.priority==0 and not a["bastion.manager.list"].options.requiresSelection and
  not a["bastion.manager.list"].options.requiresReview,"List metadata requires an unrelated selection or review")
 for _,id in ipairs({"grant","revoke"})do
  local metadata=a["bastion.manager."..id]
  check(metadata.options.requiresSelection and metadata.options.requiresReview,"Delegation metadata omitted explicit selection or review")
 end
 local ordered=service.ListActions("RaidWindow","member_actions")
 table.sort(ordered,function(left,right)return left.options.priority<right.options.priority end)
 check(ordered[1].id=="bastion.manager.list"and ordered[2].id=="bastion.manager.grant"and ordered[3].id=="bastion.manager.revoke",
  "Stock actions are not explicitly ordered List, Grant, Revoke")
 check(ordered[2].label=="Grant manager"and ordered[2].options.priority==10 and
  ordered[3].label=="Revoke manager"and ordered[3].options.priority==20,"Stock delegation labels or explicit priorities changed")
 frame();service.ListActions("RaidWindow","member_actions")
 check(registrations==3 and stateReads==2 and #native==0,"Repeated metadata paint registered duplicates or invoked the backend")
 local result=service.InvokeAction(a["bastion.manager.list"].handle,"invoke",{})
 local call=native[#native]
 check(result.ok==true and type(result.ok)=="boolean"and call[1]=="RaidWindow"and call[2]=="bastion.manager.list"and
  call[3]=="invoke"and call[4]==""and call[5]=="","List callback lost the five-string native contract")
 response={ok=1,message="Grant raid management to MiRa?",review_token="opaque:18446744073709551615"}
 local context={selection={name="MiRa",offzone=true}}
 result=service.InvokeAction(a["bastion.manager.grant"].handle,"review",context)
 call=native[#native]
 check(result.ok==true and result.reviewToken==response.review_token and result.review_token==nil and
  result.message==response.message and call[4]=="MiRa"and call[3]=="review",
  "Stock review lost canonical selection, exact token or camel-case result mapping")
 local token=result.reviewToken
 local pendingRegistrations,pendingCalls=registrations,#native
 frame();frame()
 check(registrations==pendingRegistrations and #native==pendingCalls and count()==3,
  "Fresh source-bound method wrappers churned stock handles or invoked a pending review on paint")
 response={ok=1,message="Request sent. Check server chat for confirmation."}
 result=service.InvokeAction(a["bastion.manager.grant"].handle,"confirm",context,token)
 call=native[#native]
 check(result.ok==true and call[3]=="confirm"and call[4]=="MiRa"and call[5]==token and state.settings.enabled==0 and state.settings.manage==0,
  "Explicit stock confirmation lost its opaque token or changed overlay settings")
 response={ok=1,message="Revoke raid management from MiRa?",review_token="revoke-token"}
 result=service.InvokeAction(a["bastion.manager.revoke"].handle,"review",context)
 check(result.reviewToken=="revoke-token"and native[#native][2]=="bastion.manager.revoke","Revoke callback invoked the wrong action")
 response={ok=1,message="Delegation review cancelled."}
 result=service.InvokeAction(a["bastion.manager.revoke"].handle,"cancel",context,"revoke-token")
 check(result.ok==true and native[#native][3]=="cancel"and native[#native][5]=="revoke-token","Cancel lost its exact review scope")
 response={ok=0,message="The raid roster changed."}
 result=service.InvokeAction(a["bastion.manager.grant"].handle,"confirm",context,"expired")
 check(result.ok==false and result.message==response.message,"Numeric backend zero became a truthy Lua success")
 response=nil
 check(service.InvokeAction(a["bastion.manager.list"].handle,"invoke",{}).ok==false,"Missing backend result fabricated success")
 response={ok=1,message="Missing token"}
 check(service.InvokeAction(a["bastion.manager.grant"].handle,"review",context).ok==false,"Review succeeded without an opaque token")
 local before=#native
 check(service.InvokeAction(a["bastion.manager.grant"].handle,"invoke",context).ok==false and #native==before,
  "A grant invoked directly without its review phase")
 local staleCallback=registry[a["bastion.manager.grant"].handle].callback
 current=nil
 check(service.InvokeAction(a["bastion.manager.list"].handle,"invoke",{}).ok==false and #native==before,
  "Callback cached an unloaded native plugin")
 frame();check(count()==0 and removed==3,"Plugin retirement retained stock handles")
 check(staleCallback("RaidWindow","bastion.manager.grant","confirm",context,token).ok==false and #native==before,
  "A retired closure invoked the backend")
 current=plugin;frame();check(count()==3 and registrations==6,"Plugin reactivation did not restore stock controls")
 state.manager_supported=0;frame();check(count()==0,"Live chat permission loss retained stock actions")
 state.manager_supported=1;frame();check(count()==3,"Restored chat permission did not register fresh stock actions")
 state.in_raid=0;frame();check(count()==0,"Leaving the raid retained stock controls")
 state.in_raid=1;state.available=0;frame();check(count()==0,"Unavailable Bastion context registered stock controls")
 state.available=1;frame();check(count()==3,"Available in-raid context did not restore stock controls")
 state.in_zone=0;frame();check(count()==0,"Zoning with a stale in-raid player snapshot retained stock handles")
 state.in_zone=1;frame();check(count()==3,"Finishing zoning did not restore stock controls")
 local oldCount=registrations
 local oldRegister=service.RegisterAction
 service.RegisterAction=function(...)return oldRegister(...)end;frame()
 check(count()==3 and registrations==oldCount,"Freshly wrapped extension methods replaced live owner handles")
 local retired=actions()["bastion.manager.grant"].handle
 local retiredCallback=registry[retired].callback
 registry[retired]=nil -- The host retired this source-bound handle without a context-state change.
 oldCount=registrations;frame()
 check(count()==3 and registrations==oldCount+3 and #native==before,
  "Callback-free handle liveness did not repair silent host retirement")
 check(retiredCallback("RaidWindow","bastion.manager.grant","confirm",context,token).ok==false and #native==before,
  "A silently retired closure stayed actionable after repair")
 oldCount=registrations
 for _,action in pairs(registry)do action.enabled=false;action.selection=nil end
 frame();check(count()==3 and registrations==oldCount and #native==before,
  "Disabled metadata or missing member selection churned live registrations")
 service.IsRegistered=nil;frame();frame()
 check(count()==3 and registrations==oldCount,"Hosts without optional liveness queries lost owned-handle compatibility")
 service.IsRegistered=function(handle)return registry[handle]~=nil end
 extension=nil;facade();frame();check(count()==0,"Missing canonical extension API did not retire prior handles")
 extension={RegisterAction=service.RegisterAction};facade();frame();check(count()==0,"Registration without a matching Remove API exposed unmanaged handles")
 extension=service;facade();failId="bastion.manager.grant";frame();check(count()==0,"Failed registration left a partial stock action set")
 failId=nil;throwId="bastion.manager.revoke";frame();check(count()==0,"Revoked registration permission leaked partially registered actions")
 throwId=nil;frame();check(count()==3,"Registration did not recover after permission restoration")
 local callback=plugin.raid_manager_action;plugin.raid_manager_action=nil;frame();check(count()==0,"Missing native callback retained stock controls")
 plugin.raid_manager_action=callback;frame();check(count()==3,"Native callback restoration did not restore stock controls")
 current=nil;frame();check(count()==0 and #native==before,"Lifecycle cleanup sent a cancellation or server request on paint")
 h.restore()
end
local function source(pack)
 local file=assert(io.open(root.."/ui/"..pack.."/lib/raid_manager_actions.lua","rb"));local value=file:read("a");file:close();return value
end
check(source("modern")==source("gamepad"),"Stock Raid action helpers differ between packs")
print("Stock Raid manager UI: "..checks.." checks passed")
