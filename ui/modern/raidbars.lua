local Host=dofile("lib/bastion_host.lua")
-- Citadel raid dashboard and the guarded live overlay.
local C=dofile("lib/citadel_style.lua")
local Panel=dofile("lib/raidbar_panel.lua")
local ManagerActions=dofile("lib/raid_manager_actions.lua")
local function overlayScale()return ui.GetIO().uiScale or 1 end
BASTION_UI=BASTION_UI or {}
local message=""
local classes={"WAR","CLR","PAL","RNG","SHD","DRU","MNK","BRD","ROG","SHM","NEC","WIZ","MAG","ENC","BST","BER"}
local function plugin()
 local p=Host.Plugin("bastion");return p and p.raidbars and p or nil
end
local function set(p,key,value)
 local r=p.raidbars("set",key,value)
 message=r and r.ok==1 and ""or r and r.error or "Raid bar settings changed."
 Panel.message=message
end
ui.window("Bastion Raid Bar Settings",function()
 if not BASTION_UI.raidbars then return end
 local p=plugin();local status=p and p.raidbars("status")
 if not status or status.available~=1 or status.in_zone~=1 then BASTION_UI.raidbars=false;return end
 local state=p.raidbars("state")
 if not state or state.available~=1 or state.in_zone~=1 then BASTION_UI.raidbars=false;return end
 local io=ui.GetIO();local scale=C.scale()
 local width=math.min(1100*scale,(io.width or 1280)-40*scale)
 local height=math.min(740*scale,(io.height or 720)-60*scale)
 ui.SetNextWindowPos(((io.width or 1280)-width)/2,((io.height or 720)-height)/2,ui.Cond.FirstUseEver)
 local visible,open=C.begin("Raid Bar Settings",true,1100,740);BASTION_UI.raidbars=open
 if visible then Panel.draw(p,state,scale)end
 C.finish()
end)
local Overlay=dofile("lib/raidbar_overlay.lua")
ui.window("Bastion Raid Bars",function()
 local p=plugin();local status=p and p.raidbars("status")
 ManagerActions.sync(p,status)
 if status and status.available==1 and status.in_zone==1 and status.in_raid==1 and (status.settings or{}).enabled==1 then
  local state=p.raidbars("state")
  if state then Overlay.draw(p,state,overlayScale(),set)end
 end
end)
