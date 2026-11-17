local Host=dofile("lib/bastion_host.lua")
local R=dofile("lib/citadel_received.lua")
local draw=dofile("lib/telegraphs.lua")
BASTION_UI=BASTION_UI or {}
ui.window("Bastion Telegraphs",function()
 if not BASTION_UI.telegraphs then return end
 local p=Host.Plugin("bastion");local state=p and p.telegraphs and p.telegraphs("state")
 if not state or state.active~=true then BASTION_UI.telegraphs=false;return end
 local visible,open=R.begin("Bastion Telegraphs",1080,820)
 if visible then draw(p,state,R.s)end
 BASTION_UI.telegraphs=open
 if R.finish()then BASTION_UI.telegraphs=false end
end)
