-- The live raid display uses copied host observations. Drawing never targets or polls remote actors.
local M={message="",pending=nil}
local palette={{.12,.56,1},{1,1,.94},{.25,.4,.9},{.13,.7,.3},{.55,.25,.85},{.68,1,.18},{.65,.42,.2},{1,.18,.58},{.86,.08,.24},{1,.55,0},{.7,.2,.7},{1,.2,.2},{.3,.45,1},{0,1,1},{.85,.65,.13},{.65,.3,.3}}
local function known(row,key)return row[key.."_known"]==1 end
local function live(row)return (row.spawn_id or 0)>0 and row.incarnation and row.incarnation~="0"end
local function action(p,state,name,kind,group)
 local r=p.raidbars("action",state.roster_revision,name,kind,group)
 M.message=r and r.ok==1 and "Request sent; waiting for the raid roster."or r and r.error or "Raid action is unavailable."
 M.pending=nil
end
function M.cards(state)
 local s=state.settings or {};local list=state.rows or {};local cards={}
 if s.group_sort==1 then
  local groups={};for g=0,12 do groups[g]={}end
  for _,row in ipairs(list)do local g=row.group or 0;if groups[g]then table.insert(groups[g],row)end end
  for g=1,12 do
   local rows=groups[g];local members=0;for _,row in ipairs(rows)do if row.pet~=1 then members=members+1 end end
   local occupied=state.group_counts and state.group_counts[g+1]or members
   if #rows>0 or s.show_empty==1 or s.manage==1 then
    local copy={};for _,row in ipairs(rows)do table.insert(copy,row)end
    if s.show_empty==1 or s.manage==1 then for slot=occupied+1,6 do table.insert(copy,{empty=true,group=g,slot=slot})end end
    table.insert(cards,{group=g,label="Group "..g.."  "..occupied.."/6",rows=copy})
   end
  end
  if #groups[0]>0 then table.insert(cards,{group=0,label="Ungrouped",rows=groups[0]})end
 else
  local per=math.max(1,s.rows or 12)
  for i,row in ipairs(list)do
   local index=math.floor((i-1)/per)+1
   if not cards[index]then cards[index]={label="Class priority",rows={}}end
   table.insert(cards[index].rows,row)
  end
 end
 return cards
end
local function firstEmpty(state)
 local used={};for _,row in ipairs(state.rows or {})do if row.pet~=1 then used[row.group or 0]=true end end
 for g=1,12 do if state.group_counts and state.group_counts[g+1]==0 or not state.group_counts and not used[g]then return g end end
end
local function manageClick(p,state,row,io)
 if row.pet==1 or state.settings.manage~=1 then return false end
 if io.alt and not io.ctrl and not io.shift then action(p,state,row.name,1,0);return true end
 if io.shift and not io.ctrl and not io.alt then
  if (row.group or 0)==0 then
   local g=firstEmpty(state);if g then action(p,state,row.name,1,g)else M.message="All twelve raid groups are occupied."end
  else action(p,state,row.name,2,row.group or 0)end
  return true
 end
 if io.ctrl and not io.alt and not io.shift then
  if M.pending then action(p,state,M.pending.name,1,row.group or 0)
  else M.pending={name=row.name,revision=state.roster_revision};M.message="Selected "..row.name..". Choose a destination group."end
  return true
 end
 return false
end
local function fit(text,width)
 if ui.CalcTextSize(text)<=width then return text end
 local at=#text
 while at>0 and ui.CalcTextSize(text:sub(1,at).."...")>width do
  at=at-1;while at>0 and (text:byte(at+1)&0xc0)==0x80 do at=at-1 end
 end
 return text:sub(1,at).."..."
end
local function target(p,state,row)
 local r=p.raidbars("select",state.roster_revision,row.spawn_id,row.incarnation)
 M.message=r and r.ok==1 and ""or r and r.error or "That raid actor changed."
end
local function menu(p,state,row)
 if not ui.BeginPopupContextItem("##raidmenu:"..row.name..":"..(row.spawn_id or 0))then return end
 ui.Text(row.name)
 if live(row)and ui.Selectable("Target##raidmenu:"..row.name)then target(p,state,row)end
 if row.pet~=1 and state.settings.manage==1 then
  if ui.Selectable("Move to ungrouped##raidmenu:"..row.name)then action(p,state,row.name,1,0)end
  if ui.Selectable("Promote group leader##raidmenu:"..row.name)then action(p,state,row.name,2,row.group or 0)end
  for g=1,12 do if ui.Selectable("Move to group "..g.."##raidmenu:"..row.name)then action(p,state,row.name,1,g)end end
 end
 ui.EndPopup()
end
local function bar(p,state,row,x,y,w,h,scale,pet)
 ui.SetCursorScreenPos(x,y)
 if row.empty then
  if ui.Button("Empty##raidempty:"..row.group..":"..row.slot,w,h)and M.pending then action(p,state,M.pending.name,1,row.group)end
  return
 end
 local suffix=known(row,"health")and ("  "..(row.health or 0).."%")or "  unavailable"
 local hp=math.max(0,math.min(100,row.health or 0))
 local color=not known(row,"health")and {.3,.32,.36}or hp<=25 and {.85,.18,.23}or hp<=50 and {.84,.53,.12}or {.14,.5,.29}
 ui.PushStyleColor(ui.Col.PlotHistogram,color[1],color[2],color[3],.9)
 ui.ProgressBar(known(row,"health")and hp/100 or 0,w,h,"")
 ui.PopStyleColor()
 local label=row.name or "Unknown member"
 local flags=row.flags or 0
 if not pet then
  if flags&1~=0 then label="[RL] "..label elseif flags&2~=0 then label="[GL] "..label end
  if flags&4~=0 then label=label.." [A]"end
  if flags&8~=0 then label=label.." [M]"end
 end
 local textColor=(state.settings.class_colors==1 and not pet and live(row))and palette[row.class_id or 0]or nil
 if not textColor then textColor=live(row)and {.96,.96,.96}or {.62,.64,.68}end
 local available=w-8*scale;local suffixWidth=ui.CalcTextSize(suffix)
 local caption=suffixWidth>=available and fit(suffix,available)or fit(label,available-suffixWidth)..suffix
 ui.DrawText(x+4*scale,y+math.max(0,(h-14*scale)/2),textColor[1],textColor[2],textColor[3],1,caption,14*scale)
 ui.SetCursorScreenPos(x,y)
 local enabled=state.settings.edit~=1 and state.settings.clickable==1 and state.settings.clickthrough~=1
 local button="##raidactor:"..row.spawn_id..":"..row.incarnation
 if not live(row)then button=button..":"..(row.name or "")end
 if ui.InvisibleButton(button,w,h)and enabled then
  if not manageClick(p,state,row,ui.GetIO())and live(row)then target(p,state,row)end
 end
 if enabled then menu(p,state,row)end
 if ui.IsItemHovered()then ui.SetTooltip(label..(pet and " (pet)"or " - group "..(row.group or 0))..
  (known(row,"health")and ""or "\nNo current health observation").."\nMana/endurance are shown only when received.")end
end
local function rowHeight(row,s,height,scale)
 if row.empty then return height+4*scale end
 local resources=((s.mana==1 and row.pet~=1)and 5*scale or 0)+((s.endurance==1 and row.pet~=1)and 5*scale or 0)
 local pets=#(row.pets or {})
 return height+resources+pets*((s.pet_height>0 and s.pet_height or 12)*scale+2*scale)+4*scale
end
function M.draw(p,state,scale,set)
 local s=state.settings or {};if state.available~=1 or s.enabled~=1 or #(state.rows or {})==0 then return end
 if s.manage~=1 or s.edit==1 then M.pending=nil end
 if M.pending and M.pending.revision~=state.roster_revision then M.pending=nil;M.message="Raid roster changed; select the member again."end
 scale=scale or 1
 local cards=M.cards(state);local columns=math.max(1,math.min(#cards,s.columns or 6))
 local width=(s.bar_width>0 and s.bar_width or 150)*scale
 local height=(s.bar_height>0 and s.bar_height or 28)*scale
 local petWidth=(s.pet_width>0 and s.pet_width*scale or width)
 local cardWidth=math.max(width,petWidth)+12*scale
 local cardHeights={};local lineHeights={}
 for i,card in ipairs(cards)do
  local size=30*scale;for _,row in ipairs(card.rows)do size=size+rowHeight(row,s,height,scale)end
  cardHeights[i]=size;local line=math.floor((i-1)/columns)+1;lineHeights[line]=math.max(lineHeights[line]or 0,size)
 end
 local contentHeight=0;for _,h in ipairs(lineHeights)do contentHeight=contentHeight+h+6*scale end
 local io=ui.GetIO();local naturalW=columns*cardWidth+18*scale
 local naturalH=contentHeight+44*scale
 local w=(s.right or 0)>(s.left or 0)and (s.right-s.left)*scale or naturalW
 local h=(s.bottom or 0)>(s.top or 0)and (s.bottom-s.top)*scale or naturalH
 w=math.min(io.width,math.max(160*scale,w));h=math.min(io.height,math.max(100*scale,h))
 local x=math.max(0,math.min(s.left*scale,io.width-w));local y=math.max(0,math.min(s.top*scale,io.height-h))
 ui.SetNextWindowPos(x,y,s.edit==1 and ui.Cond.Appearing or ui.Cond.Always)
 ui.SetNextWindowSize(w,h,s.edit==1 and ui.Cond.Appearing or ui.Cond.Always)
 ui.SetNextWindowBgAlpha((s.background or 80)/100)
 local flags=ui.WindowFlags.NoSavedSettings|ui.WindowFlags.NoCollapse|(ui.WindowFlags.HorizontalScrollbar or 0)
 if s.edit~=1 then flags=flags|ui.WindowFlags.NoResize|ui.WindowFlags.NoTitleBar|ui.WindowFlags.NoMove|ui.WindowFlags.NoFocusOnAppearing end
 if s.clickthrough==1 and s.edit~=1 then flags=flags|ui.WindowFlags.NoInputs end
 if ui.Begin("Bastion raid bars###bastionraidbars",nil,flags)then
  if naturalW>w or naturalH>h then ui.Text("Raid extends beyond this area; scroll or enlarge in Edit mode.")end
  if M.message~=""then ui.TextWrapped(M.message)end
  local ox,oy=ui.GetCursorScreenPos();local lineTop={};local offset=0
  for line,lineHeight in ipairs(lineHeights)do lineTop[line]=offset;offset=offset+lineHeight+6*scale end
  for i,card in ipairs(cards)do
   local col=(i-1)%columns;local line=math.floor((i-1)/columns)+1
   local bx,by=ox+col*cardWidth,oy+lineTop[line]
   ui.DrawRectFilled(bx,by,bx+cardWidth-4*scale,by+cardHeights[i],.06,.07,.09,.65,4*scale)
   ui.SetCursorScreenPos(bx+4*scale,by+4*scale)
   if ui.Button(card.label.."##raidgroup:"..i,width,22*scale)and M.pending and card.group~=nil then action(p,state,M.pending.name,1,card.group)end
   local yy=by+30*scale
   for _,row in ipairs(card.rows)do
    bar(p,state,row,bx+4*scale,yy,width,height,scale,row.pet==1)
    local resourceY=yy+height
    for _,resource in ipairs({{"mana",{.22,.45,.9}},{"endurance",{.85,.63,.13}}})do
     local key,color=resource[1],resource[2]
     if not row.empty and row.pet~=1 and s[key]==1 then
      if known(row,key)then
       ui.SetCursorScreenPos(bx+4*scale,resourceY)
       ui.PushStyleColor(ui.Col.PlotHistogram,color[1],color[2],color[3],1)
       ui.ProgressBar(math.max(0,math.min(100,row[key]or 0))/100,width,4*scale,"");ui.PopStyleColor()
      end
      resourceY=resourceY+5*scale
     end
    end
    for _,pet in ipairs(row.pets or {})do
     local ph=(s.pet_height>0 and s.pet_height or 12)*scale
     bar(p,state,pet,bx+4*scale,resourceY,petWidth,ph,scale,true);resourceY=resourceY+ph+2*scale
    end
    yy=yy+rowHeight(row,s,height,scale)
   end
  end
  ui.SetCursorScreenPos(ox,oy+contentHeight);ui.Dummy(naturalW-18*scale,1)
  if s.edit==1 and not ui.IsMouseDown(ui.Mouse.Left)then
   local px,py=ui.GetWindowPos();local ww,hh=ui.GetWindowSize()
   local left,top=math.floor(px/scale+.5),math.floor(py/scale+.5)
   local right,bottom=left+math.floor(ww/scale+.5),top+math.floor(hh/scale+.5)
   for key,value in pairs({left=left,top=top,right=right,bottom=bottom})do if math.abs(value-(s[key]or 0))>1 then set(p,key,value)end end
  end
 end
 ui.End()
end
return M
