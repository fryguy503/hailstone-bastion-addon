local Host=dofile("lib/bastion_host.lua")
-- Citadel raid dashboard and the guarded live overlay.
local C=dofile("lib/citadel_style.lua")
local Panel=dofile("lib/raidbar_panel.lua")
local ManagerActions=dofile("lib/raid_manager_actions.lua")
local W=dofile("lib/window.lua")
local T=W.T
local function overlayScale()T.frame();return T.S end
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
 T.frame();local scale=T.S;local io=ui.GetIO();C.push()
 if W.begin("Raid Bar Settings",{w=math.min(1460,((io.width or 1280)-32*scale)/scale),h=math.min(1000,((io.height or 720)-32*scale)/scale),
     title="Raid bars",subtitle="Citadel raid overview",hints={{"a","Choose"},{"b","Close"}}})then
  local x,y,_,_,width,height=W.area();ui.SetCursorScreenPos(x,y)
  if ui.BeginChild("##raidbar-shell",width,height,false)then Panel.draw(p,state,C.scale(),true)end
  ui.EndChild()
 end
 if W.finish()then BASTION_UI.raidbars=false end
 C.pop()
end)
local Overlay=dofile("lib/raidbar_overlay.lua")
local Ready=dofile("lib/raid_ready_check.lua")
ui.window("Bastion Raid Bars",function()
 local p=Host.Plugin("bastion")
 local status=p and p.raidbars and p.raidbars("status")
 ManagerActions.sync(p,status)
 local ready=Ready.read(p)
 local results=Ready.results(ready)
 if status and status.available==1 and status.in_zone==1 and status.in_raid==1 and (status.settings or{}).enabled==1 then
  local state=p.raidbars("state")
  if state then Overlay.draw(p,state,overlayScale(),set,results)end
 end
 if Ready.visible(ready) then
  T.frame();local scale=T.S;local io=ui.GetIO();C.push()
  local width,height=io.width or 1280,io.height or 720
  local short=height<420*scale
  if W.begin("Raid Ready Check",{w=math.min(960,math.max(1,width-32*scale)/scale),
    h=math.min(results and 695 or 295,math.max(1,height-32*scale)/scale),
    title="Raid Ready Check",alwaysFocus=true,padding=short and 14 or nil,
    hints=not short and {{"a","Choose"},{"b","Close"}} or nil})then
   local x,y,_,_,width,height=W.area();ui.SetCursorScreenPos(x,y)
   if ui.BeginChild("##raid-ready-shell",width,height,false)then Ready.draw(p,ready,results,C)end
   ui.EndChild()
  end
  if W.finish()then Ready.close()end
  C.pop()
 end
end)
