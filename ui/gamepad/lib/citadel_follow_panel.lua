local C=dofile("lib/citadel_style.lua")
local message=""
local function invoke(p,...)
 local result=p.citadel_follow(...)
 message=result and (result.error or "") or "Follow controls are unavailable."
end
local function toggle(p,state,key,label)
 local value,changed=ui.Checkbox(label,state[key]==1)
 if changed then invoke(p,"set",key,value and 1 or 0) end
end
return function(p,state,scale,framed)
 if not framed then C.header("FOLLOW & MOUNTS","Movement preferences and manual follow controls") end
 local _,remaining=ui.GetContentRegionAvail()
 local _,lineHeight=ui.CalcTextSize("Ag")
 local footer=math.max(80*scale,lineHeight*2+54*C.scale())
 local height=math.max(160*scale,remaining-footer)
 C.split("citadel-follow",ui.GetContentRegionAvail()*0.45,height,function()
  C.section("FOLLOW")
  toggle(p,state,"enabled","Enhanced follow")
  toggle(p,state,"cancel_on_move","Cancel on movement")
  ui.Dummy(0,7*scale)
  C.field("Distance",string.format("%.1f units",state.distance or 15),"gold")
  ui.SetNextItemWidth(-1)
  local distance,changed=ui.SliderFloat("##follow-distance",state.distance or 15,1,50)
  if changed then invoke(p,"set","distance",distance) end
  C.note("Adjust the distance between you and the selected player (1-50).","muted")
  ui.Dummy(0,14*scale)
  C.section("MOUNT RESPONSE")
  toggle(p,state,"instant_mount","Instant mount response")
  C.tooltip("Removes mount acceleration and stopping delay without changing its permitted speed.")
  C.note("Your mount's normal speed and your walk/run choice are preserved.","muted")
 end,function()
  C.section("SELECTED PLAYER")
  C.note(state.selected_name or "No player selected",state.can_start==1 and "blue" or "muted")
  if state.selected_distance then C.field("Distance away",string.format("%.1f units",state.selected_distance)) end
  ui.Dummy(0,14*scale)
  C.section("FOLLOW STATUS")
  C.note(state.following==1 and (state.enhanced==1 and "Enhanced follow active" or "Stock follow active") or "Stopped",state.following==1 and "good" or "gold")
  if state.following_name then C.field("Following",state.following_name) end
  ui.Dummy(0,10*scale)
  local x,y=ui.GetCursorScreenPos();local width=ui.GetContentRegionAvail();local cx=x+width*0.5
  ui.DrawLine(x+28*scale,y+24*scale,x+width-28*scale,y+24*scale,0.25,0.39,0.48,1,2)
  ui.DrawCircleFilled(x+28*scale,y+24*scale,9*scale,0.38,0.72,0.96,1)
  ui.DrawCircle(x+width-28*scale,y+24*scale,11*scale,0.96,0.75,0.36,1,2)
  ui.DrawLine(cx-10*scale,y+24*scale,cx+10*scale,y+24*scale,0.96,0.75,0.36,1,2)
  ui.DrawLine(cx+10*scale,y+24*scale,cx+3*scale,y+17*scale,0.96,0.75,0.36,1,2)
  ui.DrawLine(cx+10*scale,y+24*scale,cx+3*scale,y+31*scale,0.96,0.75,0.36,1,2)
  ui.Dummy(width,52*scale)
  C.note("Follow moves toward the player in a straight line. Select your target with the normal game controls.","muted")
 end)
 ui.Separator()
 local bw=(ui.GetContentRegionAvail()-10*scale)/2
 if C.button("Follow selected player",bw,true,state.can_start==1,32*scale) then invoke(p,"start") end
 ui.SameLine()
 if C.button("Stop following",bw,"danger",state.following==1,32*scale) then invoke(p,"stop") end
 if message~="" then C.note(message,"warn")
 elseif state.error and state.error~="" then C.note(state.error,"warn")
 else C.note("Follow starts manually. Login and zoning leave it stopped.","muted") end
end
