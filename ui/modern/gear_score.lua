local Host=dofile("lib/bastion_host.lua")
local R=dofile("lib/citadel_received.lua")
local draw=dofile("lib/gear_score.lua")
BASTION_UI=BASTION_UI or{}
ui.window("Bastion Item Ratings",function()
  if not BASTION_UI.gear_score then return end
  local p=Host.Plugin("bastion");local s=p and p.gear_score and p.gear_score("state")
  if not s or s.active~=1 then BASTION_UI.gear_score=false;return end
  local visible,open=R.begin("Bastion Item Ratings",850,580)
  BASTION_UI.gear_score=open
  if visible then draw(p,s,R.s)end
  if R.finish()then BASTION_UI.gear_score=false end
end)
