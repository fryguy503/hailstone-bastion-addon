local Host=dofile("lib/bastion_host.lua")
local C=dofile("lib/citadel_style.lua")
local draw=dofile("lib/citadel_follow_panel.lua")
BASTION_UI=BASTION_UI or {}
ui.window("Bastion Follow",function()
 if not BASTION_UI.citadel_follow then return end
 local p=Host.Plugin("bastion");local state=p and p.citadel_follow and p.citadel_follow("state")
 if not state or state.active~=1 then BASTION_UI.citadel_follow=false;return end
 local visible,open=C.begin("Bastion Follow",true,760,500)
 if visible then draw(p,state,C.scale()) end
 C.finish();BASTION_UI.citadel_follow=open
end)
