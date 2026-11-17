local C=dofile("lib/citadel_style.lua")
local M={page=1,rule=1,message="",priority=nil,member=nil,delegate_member=nil}
local classes={"WAR","CLR","PAL","RNG","SHD","DRU","MNK","BRD","ROG","SHM","NEC","WIZ","MAG","ENC","BST","BER"}
local function set(p,key,value)
 local r=p.raidbars("set",key,value);M.message=r and r.ok==1 and ""or r and r.error or "The setting could not be saved."
end
local function toggle(p,s,key,label)
 local v,changed=ui.Checkbox(label.."##rb:"..key,s[key]==1);if changed then set(p,key,v and 1 or 0)end
end
local function fields(id,draw)
 if ui.BeginTable(id,2,ui.TableFlags.SizingStretchProp)then
  ui.TableSetupColumn("Setting",ui.TableColumnFlags.WidthStretch,1.2);ui.TableSetupColumn("Value",ui.TableColumnFlags.WidthStretch,1)
  draw();ui.EndTable()
 end
end
local function slider(p,s,key,label,min,max)
 ui.TableNextRow();ui.TableNextColumn();ui.Text(label);ui.TableNextColumn();ui.SetNextItemWidth(-1)
 local v,changed=ui.SliderInt("##rb:"..key,s[key]or min,min,max);if changed then set(p,key,v)end
end
local function fit(text,width)
 if ui.CalcTextSize(text)<=width then return text end
 local at=#text
 while at>0 and ui.CalcTextSize(text:sub(1,at).."...")>width do
  at=at-1
  while at>0 and (text:byte(at+1)&0xc0)==0x80 do at=at-1 end
 end
 return text:sub(1,at).."..."
end
local samples={{name="Mira",group=1,class_id=2,health_known=1,health=83,mana_known=1,mana=65,pets={}},
 {name="Oren",group=1,class_id=1,health_known=1,health=46,mana_known=0,pets={}},
 {name="Elara",group=1,class_id=13,health_known=1,health=92,mana_known=1,mana=74,pets={}},
 {name="Wren",group=2,class_id=4,health_known=0,mana_known=0,pets={}}}
local function preview(state,s,scale)
 local list=state.rows or {};local example=#list==0
 if example then list=samples end
 C.section("Raid preview",example and "Example layout - no matching members"or #list.." members currently shown")
 C.field("Grid",(s.rows or 12).." rows x "..(s.columns or 6).." columns","gold")
 C.field("Member bars",(s.bar_width or 150).." x "..(s.bar_height or 28).." px")
 ui.Spacing()
 local columns=math.min(3,math.max(1,s.columns or 6))
 if ui.BeginTable("##rb-preview-grid",columns,ui.TableFlags.SizingStretchProp)then
  for i=1,math.min(9,#list)do
   local member=list[i];ui.TableNextColumn();local width=ui.GetContentRegionAvail()
   C.note(fit(member.name or "Unknown member",math.max(20,width-8*scale)),"text")
   local hp=member.health_known==1 and math.max(0,math.min(100,member.health or 0))or nil
   C.gauge("",hp and hp/100 or nil,hp and hp.."%"or "Unavailable",hp and (hp<=25 and "danger"or hp<=50 and "warn"or "good")or "muted",22*scale)
   if s.mana==1 and member.mana_known==1 then C.gauge("",math.max(0,math.min(100,member.mana or 0))/100,"","blue",4*scale)end
   if s.show_pets==1 and s.combine_pets==1 and (member.pets or {})[1]then
    local pet=member.pets[1];C.note(fit(pet.name or "Companion",math.max(20,width-8*scale)),"muted")
    local value=pet.health_known==1 and math.max(0,math.min(100,pet.health or 0))or nil
    C.gauge("",value and value/100 or nil,value and value.."%"or "Unavailable","good",18*scale)
   end
   ui.Dummy(0,8*scale)
  end
  ui.EndTable()
 end
 ui.Spacing();C.note("Off-zone health is unavailable. Click a live overlay bar to target.","muted")
end
local function overview(p,s)
 C.section("Raid display","Overlay and mouse behavior")
 for _,v in ipairs({{"enabled","Show raid bars"},{"edit","Edit overlay position"},{"clickable","Click bars to target"},{"clickthrough","Let clicks pass through"},
   {"show_all","Show all raid members"},{"group_sort","Sort by raid group"},{"show_self","Include yourself in group cards"},
   {"show_empty","Show empty group slots"},{"class_colors","Class-colored names"},{"mana","Show observed mana"},{"endurance","Show observed endurance"}})do toggle(p,s,v[1],v[2])end
 fields("##rb-display-fields",function()slider(p,s,"background","Background %",0,100)end)
 ui.Spacing();C.section("Pets")
 toggle(p,s,"show_pets","Show raid pets");toggle(p,s,"combine_pets","Place pets under their owner")
  C.note("Edit mode lets you drag and resize the overlay. Changes save when you release the mouse. Off-zone and unknown vitals remain unavailable.","muted")
end
local function layout(p,s)
 C.section("Members & pets","Use 0 for the default size")
 fields("##rb-dimensions",function()
  slider(p,s,"bar_width","Member width",0,500);slider(p,s,"bar_height","Member height",0,80)
  slider(p,s,"pet_width","Pet width",0,500);slider(p,s,"pet_height","Pet height",0,50)
 end)
 ui.Spacing();C.section("Position & grid")
 fields("##rb-position",function()
  slider(p,s,"left","Left",0,4096);slider(p,s,"top","Top",0,4096)
  slider(p,s,"rows","Rows per column",1,72);slider(p,s,"columns","Maximum columns",1,72)
 end)
  toggle(p,s,"edit","Drag or resize overlay")
end
local function rules(p,s,scale)
 C.section("Class rules","Never overrides Always and Show all")
  toggle(p,s,"filter_groups","Apply class and HP rules to group cards")
  C.note("By default group cards show the complete roster, as Citadel does. HP filters use strictly below the threshold.","muted")
 fields("##rb-threshold",function()slider(p,s,"threshold","HP threshold %",0,100)end)
 M.rule=C.tabs("rb-rule",{"Always show","Never show","HP filter"},M.rule)
 local keys={"always","never","filter"};local key=keys[M.rule];local mask=s[key]or 0
 if ui.BeginTable("##rb-class-grid",4,ui.TableFlags.SizingStretchProp)then
  for id,name in ipairs(classes)do
   ui.TableNextColumn();local bit=1<<(id-1)
   local checked,changed=ui.Checkbox(name.."##rbclass:"..key..":"..id,(mask&bit)~=0)
   if changed then set(p,key,checked and (mask|bit)or (mask&~bit))end
  end
  ui.EndTable()
 end
 C.note(M.rule==1 and "These classes stay visible even at full health."or M.rule==2 and "These classes are hidden from the overlay."or "These classes use the threshold above. Other classes use 99%.","muted")
 ui.Spacing();C.section("Class priority")
 local presets={{"Healers first","CLR DRU SHM"},{"Tanks first","WAR PAL SHD"},{"Class order",""}}
 ui.SetNextItemWidth(-1)
 if ui.BeginCombo("##rb-priority-preset","Choose an order preset")then
  for _,v in ipairs(presets)do if ui.Selectable(v[1].."##rbpreset",false)then set(p,"priority",v[2]);M.priority=nil end end
  ui.EndCombo()
 end
 if M.priority==nil then M.priority=s.priority or ""end
 ui.SetNextItemWidth(-1);M.priority=ui.InputText("##rb-priority",M.priority,129)
 if C.button("Apply custom priority##rbpriority",0,true,true,28*scale)then set(p,"priority",M.priority)end
 C.tooltip("Class abbreviations separated by spaces, such as CLR WAR ENC.")
end
local function delegation(p,state,s,scale)
 C.section("Delegate raid manager","Raid leader only")
 C.note("Only the raid leader can list, grant or revoke managers. Bastion checks current membership and excludes bots and the raid leader.","muted")
 local members={};for _,row in ipairs(state.roster_members or state.rows or {})do if row.pet~=1 then members[#members+1]=row end end
 local supported=state.manager_supported==1
 local in_raid=state.in_raid==1 or #members>0
 local function request(action,name)
  local r=p.raidbars("manager",state.roster_revision,action,name or "")
  if not r or r.ok~=1 then M.message=r and r.error or "Raid manager request unavailable.";return false end
  if action=="list"then M.message="Manager list requested. Read the server reply in chat."
  elseif action=="review_add"or action=="review_remove"then M.message="Review the pending delegation below."
  elseif action=="cancel"then M.message="Delegation review cancelled."
  else M.message="Request sent. Check server chat for confirmation."end
  return true
 end
 if C.button("List raid managers##rb-manager-list",0,false,in_raid and supported,28*scale)then request("list")end
 C.note("Server chat lists the current managers and confirms changes.","muted")
 if not in_raid then C.note("Join a raid to delegate management.","warn");return end
 if not supported then C.note("This client cannot send Bastion raid manager requests.","warn");return end
 local selected=nil;for _,row in ipairs(members)do if row.name==M.delegate_member then selected=row end end
 if not selected then
  for _,row in ipairs(members)do if row.self~=1 then selected=row;break end end
  selected=selected or members[1];M.delegate_member=selected and selected.name
 end
 if not selected then return end
 C.note("Character","muted")
 ui.SetNextItemWidth(-1)
 if ui.BeginCombo("##rb-manager-member",selected.name)then
  for _,row in ipairs(members)do
   if ui.Selectable(row.name.."##rb-manager-member",row.name==M.delegate_member)then
    if row.name~=M.delegate_member and (state.manager_review or {}).active==1 then
     request("cancel");state.manager_review=nil
    end
    M.delegate_member=row.name;selected=row
   end
  end
  ui.EndCombo()
 end
 local valid=type(selected.name)=="string"and #selected.name>0 and #selected.name<64 and selected.name:match("^[A-Za-z]+$")~=nil
 local enabled=s.manage==1 and valid
 if ui.BeginTable("##rb-manager-actions",2,ui.TableFlags.SizingStretchProp)then
  ui.TableNextColumn();if C.button("Review grant##rb-manager-grant",-1,false,enabled,28*scale)then request("review_add",selected.name)end
  ui.TableNextColumn();if C.button("Review revoke##rb-manager-revoke",-1,false,enabled,28*scale)then request("review_remove",selected.name)end
  ui.EndTable()
 end
 if s.manage~=1 then C.note("Enable raid management above to grant or revoke a delegation.","muted")end
 local review=state.manager_review
 if review and review.active==1 then
  if enabled and review.name==selected.name and review.roster_revision==state.roster_revision and (review.action=="add"or review.action=="remove")then
   local granting=review.action=="add"
   ui.Spacing();C.section("Review delegation")
   C.note((granting and "Grant raid management to "or "Revoke raid management from ")..review.name.."?","text")
   C.note(granting and "This requests delegated raid administration."or "This requests removal of delegated raid administration.","muted")
   if ui.BeginTable("##rb-manager-confirmation",2,ui.TableFlags.SizingStretchProp)then
    ui.TableNextColumn();if C.button((granting and "Confirm grant"or "Confirm revoke").."##rb-manager-confirm",-1,granting and true or "danger",true,30*scale)then
     if request(granting and "confirm_add"or "confirm_remove",review.name)then state.manager_review=nil end
    end
    ui.TableNextColumn();if C.button("Cancel##rb-manager-cancel",-1,false,true,30*scale)then request("cancel");state.manager_review=nil end
    ui.EndTable()
   end
  else C.note("The roster or selection changed. Review the delegation again.","warn")end
 end
end
local function management(p,state,s,scale)
 C.section("Raid management","Mouse shortcuts and controller-friendly actions")
 toggle(p,s,"manage","Enable raid management")
 C.note("Shift: promote group leader, or place an ungrouped member in the first empty group. Alt: ungroup. Ctrl: select a member, then choose a destination group.","muted")
 C.note("Native moves follow raid lock and leader permissions. Bastion command requests also support delegated managers (#raidmanager); the server authorizes them. Changes take effect when the roster returns.","muted")
 ui.Spacing();delegation(p,state,s,scale)
 ui.Spacing();C.section("Member groups","Move and subgroup leader requests")
 local members={};for _,row in ipairs(state.rows or {})do if row.pet~=1 then members[#members+1]=row end end
 if #members==0 then C.note("Join a raid to manage its members.","warn");return end
 local selected=nil;for _,row in ipairs(members)do if row.name==M.member then selected=row end end
 if not selected then selected=members[1];M.member=selected.name end
 ui.SetNextItemWidth(-1)
 if ui.BeginCombo("##rb-manage-member",selected.name)then
  for _,row in ipairs(members)do if ui.Selectable(row.name.."##rb-member",row.name==M.member)then M.member=row.name;selected=row end end
  ui.EndCombo()
 end
 local function request(kind,group)
  local r=p.raidbars("action",state.roster_revision,selected.name,kind,group)
  M.message=r and r.ok==1 and "Request sent; waiting for the raid roster."or r and r.error or "Raid action unavailable."
 end
 ui.BeginDisabled(s.manage~=1 or state.manage_supported~=1)
 if C.button("Move to ungrouped##rb-ungroup",0,false,true,28*scale)then request(1,0)end
 if C.button("Promote group leader##rb-promote",0,false,true,28*scale)then request(2,selected.group or 0)end
 if ui.BeginTable("##rb-move-groups",3,ui.TableFlags.SizingStretchProp)then
  for g=1,12 do ui.TableNextColumn();if C.button("Group "..g.."##rb-move",0,false,true,28*scale)then request(1,g)end end
  ui.EndTable()
 end
 ui.EndDisabled()
 if state.manage_supported~=1 then C.note("This client does not expose native raid management.","warn")end
end
function M.draw(p,state,scale,framed)
 scale=scale or C.scale();local s=state.settings or {}
 if M.prioritySource~=s.priority then M.priority=s.priority;M.prioritySource=s.priority end
 if framed then C.field("Raid overlay",s.enabled==1 and "On"or "Off","gold")
 else C.header("Raid bars","Health, pets and class layouts",s.enabled==1 and "OVERLAY ON"or "OVERLAY OFF")end
  M.page=C.tabs("rb-page",{"Overview","Layout","Class rules","Manage"},M.page)
 local width,height=ui.GetContentRegionAvail();local body=math.max(100*scale,height-44*scale)
 ui.PushStyleVar(ui.StyleVar.FramePadding,6*scale,2*scale);ui.PushStyleVar(ui.StyleVar.ItemSpacing,8*scale,3*scale)
 C.split("##rb-dashboard",width*.5,body,function()
  if state.available~=1 then C.note("Enter the world to configure raid bars.","warn");return end
   if M.page==1 then overview(p,s)elseif M.page==2 then layout(p,s)elseif M.page==3 then rules(p,s,scale)else management(p,state,s,scale)end
 end,function()preview(state,s,scale)end)
 ui.PopStyleVar(2)
 ui.Separator();C.note(M.message~=""and M.message or state.error~=""and state.error or "Changes save automatically for this character.",M.message~=""and "warn"or "muted")
end
return M
