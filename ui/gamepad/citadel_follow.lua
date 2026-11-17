local Host=dofile("lib/bastion_host.lua")
local W=dofile("lib/window.lua")
local C=dofile("lib/citadel_style.lua")
local draw=dofile("lib/citadel_follow_panel.lua")
BASTION_UI=BASTION_UI or {}
ui.window("Bastion Follow",function()
 if not BASTION_UI.citadel_follow then return end
 local p=Host.Plugin("bastion");local state=p and p.citadel_follow and p.citadel_follow("state")
 if not state or state.active~=1 then BASTION_UI.citadel_follow=false;return end
 W.T.frame();C.push()
 local width=math.min(1180,(W.T.W-40)/W.T.S);local height=math.min(830,(W.T.H-40)/W.T.S)
 if W.begin("Bastion Follow",{w=width,h=height,title="Follow & mounts",subtitle="Movement preferences and manual controls",hints={{"a","Choose"},{"b","Close"}}}) then
  local x0,y0,x1,y1=W.area();local wx,wy=ui.GetWindowPos();ui.SetCursorPos(x0-wx,y0-wy)
  if ui.BeginChild("follow-page-frame",math.max(1,x1-x0),math.max(1,y1-y0),false) then draw(p,state,W.T.S,true) end
  ui.EndChild()
 end
 if W.finish() then BASTION_UI.citadel_follow=false end
 C.pop()
end)
