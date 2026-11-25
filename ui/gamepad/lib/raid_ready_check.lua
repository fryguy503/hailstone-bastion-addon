-- The server snapshot is the source of truth for both the prompt and the passive raid overlay.
local M={}
local order={"ready","not_ready","no_response","legacy"}
local labels={ready="Ready",not_ready="Not Ready",no_response="No Response",legacy="Not on modern client"}
local tones={ready="good",not_ready="danger",no_response="warn",legacy="muted"}
local colors={ready={.51,.89,.72},not_ready={1,.58,.56},no_response={.94,.76,.45},legacy={.73,.78,.84}}
local requestTimeout=5
local seen,automatic,message,pending=nil,nil,nil,nil

local function inRaid(state)
 return state and state.available==1 and state.in_raid==1
end

local function hasResults(state)
 return inRaid(state) and (state.phase=="active" or state.phase=="ended" or state.phase=="expired")
end

function M.read(p)
 if p and type(p.readycheck)=="function" then return p.readycheck("state") end
end

function M.visible(state)
 if pending and (not inRaid(state) or
  (pending.op=="start" and pending.id~=state.check_id) or
  (pending.op~="start" and (pending.id~=state.check_id or state.phase~="active")) or
  (pending.op=="respond" and pending.vote==state.self_status)) then
  pending,message=nil,nil
 elseif pending and pending.deadline and ui.GetTime()>=pending.deadline then
  pending.deadline=nil
  message="No server confirmation yet. You can try again."
 end
 if not inRaid(state) then message=nil end
 if inRaid(state) and state.phase=="active" and state.can_respond==1 and state.check_id~=seen then
  seen,automatic=state.check_id,state.check_id
  if not pending then message=nil end
 end
 return BASTION_UI.raid_ready_check or (hasResults(state) and automatic and automatic==state.check_id)
end

function M.close()
 automatic,message,pending=nil,nil,nil
 BASTION_UI.raid_ready_check=false
end

local function newCounts()
 return {ready=0,not_ready=0,no_response=0,legacy=0}
end

function M.results(state)
 if not hasResults(state) then return end
 local result={phase=state.phase,totals=newCounts(),groups={},members={}}
 for _,member in ipairs(state.members or {}) do
  local name=member.name:lower()
  local group,status=member.group,member.status
  result.members[name]=member
  local counts=result.groups[group]
  if not counts then counts=newCounts();result.groups[group]=counts end
  counts[status]=counts[status]+1
  result.totals[status]=result.totals[status]+1
 end
 return result
end

function M.detail(member)
 local detail=labels[member.status]
 if member.status=="no_response" and member.capability=="unknown" then
  return detail.." (client capability unconfirmed)"
 end
 if member.status=="no_response" and member.capability=="modern" then
  return detail.." (ready-check support unconfirmed)"
 end
 return detail
end

-- Draw these shapes directly. The game's fonts can omit Unicode status symbols.
function M.mark(x,y,size,status)
 local color=colors[status]
 local r,g,b=color[1],color[2],color[3]
 local stroke=math.max(1,size*.16)
 local function line(x0,y0,x1,y1)
  ui.DrawLine(x+x0*size,y+y0*size,x+x1*size,y+y1*size,r,g,b,1,stroke)
 end
 if status=="ready" then
  line(.1,.52,.38,.8);line(.38,.8,.92,.23)
 elseif status=="not_ready" then
  line(.22,.22,.78,.78);line(.22,.78,.78,.22)
 elseif status=="legacy" then
  ui.DrawCircle(x+size*.5,y+size*.5,size*.4,r,g,b,1,stroke)
  line(.21,.78,.79,.22)
 else
  for i=1,3 do ui.DrawCircleFilled(x+i*size*.25,y+size*.57,math.max(.65,size*.08),r,g,b,1) end
 end
end

function M.compactCounts(counts,x,y,width,scale)
 local slot=width/#order
 local size=math.min(9*scale,slot*.38)
 local font=math.min(11*scale,slot*.58)
 for i,status in ipairs(order) do
  local left=x+(i-1)*slot
  M.mark(left,y+(font-size)*.5,size,status)
  ui.DrawText(left+size+scale,y,.9,.94,.98,1,tostring(counts[status]),font)
 end
end

local function request(p,state,operation,vote)
 local response
 if operation=="start" then response=p.readycheck("start")
 elseif operation=="end" then response=p.readycheck("end",state.check_id)
 else response=p.readycheck("respond",state.check_id,vote) end
 if response and response.ok==1 then
  pending={id=state.check_id,op=operation,vote=vote,deadline=ui.GetTime()+requestTimeout}
  message="Request sent. Waiting for the server."
 else
  pending=nil
  message=response and response.error and response.error~="" and response.error or "The ready-check request could not be sent."
 end
end

local function displayResults(result,C)
 if not ui.BeginTable("##raid-ready-results",5,ui.TableFlags.SizingStretchProp or 0) then return end
 ui.TableSetupColumn("Group",0,1)
 for _,status in ipairs(order) do ui.TableSetupColumn(labels[status],0,status=="legacy" and 1.65 or 1) end
 ui.TableNextRow();ui.TableNextColumn();C.note("Group","muted")
 for _,status in ipairs(order) do ui.TableNextColumn();C.note(labels[status],tones[status]) end
 local function row(name,counts)
  ui.TableNextRow();ui.TableNextColumn();C.note(name)
  for _,status in ipairs(order) do ui.TableNextColumn();C.note(tostring(counts[status])) end
 end
 row("Raid",result.totals)
 for group=1,12 do if result.groups[group] then row("Group "..group,result.groups[group]) end end
 if result.groups[0] then row("Ungrouped",result.groups[0]) end
 ui.EndTable()
end

function M.draw(p,state,result,C)
 if not state or state.available~=1 then
  C.note("Waiting for ready-check support from the Bastion server.","muted")
  return
 end
 if state.in_raid~=1 then C.note("Join a raid to use Ready Check.","muted");return end
 if state.phase=="active" then
  if state.can_respond==1 then
   if C.button("Ready##raid-ready-vote",0,state.self_status=="ready") then request(p,state,"respond","ready") end
   ui.SameLine()
   if C.button("Not Ready##raid-ready-vote",0,state.self_status=="not_ready") then request(p,state,"respond","not_ready") end
  end
  if state.can_start==1 and C.button("End check##raid-ready-end",0,false) then request(p,state,"end") end
  local remaining=state.remaining_seconds
  C.note(remaining>0 and "Check in progress  ·  "..remaining.."s remaining" or "Waiting for ready-check results.","gold")
  if state.can_respond==1 then C.note("Your response: "..labels[state.self_status],tones[state.self_status]) end
 else
  if state.phase=="ended" then C.note("Check ended.","muted")
  elseif state.phase=="expired" then C.note("Time expired.","muted")
  elseif state.can_start~=1 then C.note("Waiting for the raid leader to start a ready check.","muted") end
  if state.can_start==1 and C.button("Start ready check##raid-ready-start",0,true) then request(p,state,"start") end
 end
 if message then C.note(message,pending and pending.deadline and "muted" or "warn") end
 local _,height=ui.GetContentRegionAvail()
 if result and height>20*C.scale() then
  ui.Separator()
  if ui.BeginChild("##raid-ready-report",0,0,false) then displayResults(result,C) end
  ui.EndChild()
 end
end

return M
