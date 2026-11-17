local Host=dofile("lib/bastion_host.lua")
-- Shared layout primitives for the four received-information tools. All actions
-- remain in their owning windows; drawing never requests or changes game state.
local P=dofile("lib/received_tools.lua")
local C=dofile("lib/citadel_style.lua")
local R={C=C}
function R.begin(title,w,h)
  C.push()
  local visible,open=P.begin(title,w,h)
  R.s=P.scale;R.width=P.width;R.height=P.height
  return visible,open
end
function R.finish()local closed=P.finish();C.pop();return closed end
function R.height_for(wanted,reserve,minimumReserve)
  local _,h=ui.GetContentRegionAvail()
  return math.max(60*R.s,math.min(wanted*R.s,h-math.max((reserve or 0)*R.s,minimumReserve or 0)))
end
function R.status(s,ready)
  if s.pending then C.note("Updating...","warn")
  elseif not s.available then C.note("Enter Bastion to use this window.","muted")
  elseif not s.fresh then C.note("Refresh to update this information.","warn")
  elseif ready then C.note(ready,"good")
  elseif s.message and s.message~="" then C.note(s.message,"muted")end
  if not s.fresh and not s.pending and s.message and s.message~=""then C.note(s.message,"muted")end
end
function R.table(id,columns,draw)
  local f=ui.TableFlags or {}
  if ui.BeginTable(id,#columns,(f.RowBg or 0)|(f.BordersInnerH or 0)|(f.SizingStretchProp or 0))then
    for _,column in ipairs(columns)do
      if type(column)=="table"then ui.TableSetupColumn(column[1],0,column[2])else ui.TableSetupColumn(column)end
    end
    ui.TableHeadersRow();draw();ui.EndTable()
  end
end
function R.cell()ui.TableNextColumn()end
function R.narrow()
  local width=ui.GetContentRegionAvail()
  return width<620*math.max(R.s or 1,C.scale())
end
function R.split(id,leftWidth,height,left,right,compactLeftHeight)
  local width=ui.GetContentRegionAvail()
  if R.narrow()then
    local gap=10*math.max(R.s,C.scale())+1
    local room=math.max(2,height-gap)
    local first=math.min(room/2,compactLeftHeight or room/2)
    C.card(id.."-left",0,first,function()left(true)end)
    C.card(id.."-right",0,room-first,function()right(true)end)
  else
    C.card(id.."-left",math.min(leftWidth,math.max(120*R.s,width*0.72)),height,function()left(false)end)
    ui.SameLine();C.card(id.."-right",0,height,function()right(false)end)
  end
end
function R.icon(icon,size,animation)
  local tex,u0,v0,u1,v1
  if icon and Host.ItemIcon then tex,u0,v0,u1,v1=Host.ItemIcon(icon)end
  if not tex and animation and Host.UiAnimation then tex,u0,v0,u1,v1=Host.UiAnimation(animation)end
  if tex then ui.Image(tex,size,size,u0 or 0,v0 or 0,u1 or 1,v1 or 1)else ui.Dummy(size,size)end
end
function R.category(entry,selected)
  local id="##category"..entry.id
  local function animation(name)
    if not Host.UiAnimation or not name or name==""then return end
    local tex,u0,v0,u1,v1,w,h=Host.UiAnimation(name)
    if tex and w and h and w>0 and h>0 then return {tex,u0,v0,u1,v1,w,h}end
  end
  local base=animation(entry.icon)
  local image=selected and entry.icon and entry.icon~=""and animation(entry.icon.."Selected")or nil
  image=image or base
  if not image then return ui.Selectable(entry.name..id,selected)end
  local available=ui.GetContentRegionAvail()
  local factor=math.min(1,math.min(available,180*R.s)/image[6],32*R.s/image[7])
  local w,h=image[6]*factor,image[7]*factor
  local x,y=ui.GetCursorScreenPos()
  local clicked=ui.Selectable(id,selected,0,available,math.max(22*R.s,h+4*R.s))
  if not selected and ui.IsItemHovered()then image=animation((entry.icon or "").."Hover")or image end
  ui.DrawImage(image[1],x+4*R.s,y+2*R.s,x+4*R.s+w,y+2*R.s+h,image[2],image[3],image[4],image[5])
  if image[6]/image[7]<2.5 then
    local t=C.colors.text
    ui.PushClipRect(x,y,x+available,y+math.max(22*R.s,h+4*R.s),true)
    ui.DrawText(x+w+10*R.s,y+2*R.s,t[1],t[2],t[3],t[4],entry.name)
    ui.PopClipRect()
  end
  C.tooltip(entry.name)
  return clicked
end
function R.time(value)
  local seconds=tonumber(value)
  if not seconds or seconds<0 then return "Unavailable"end
  if seconds>=86400 then return string.format("%dd %dh",math.floor(seconds/86400),math.floor(seconds%86400/3600))end
  if seconds>=3600 then return string.format("%dh %dm",math.floor(seconds/3600),math.floor(seconds%3600/60))end
  return string.format("%dm %ds",math.floor(seconds/60),math.floor(seconds%60))
end
function R.actions(actions)
  local available=ui.GetContentRegionAvail()
  -- The controller frame supplies wider padding than the desktop style, and
  -- its design scale can differ from the host UI scale. Allow both plus rounding.
  local unit=math.max(R.s,C.scale())
  local used,gap=0,10*unit+1
  for _,action in ipairs(actions)do
    local label=action.label:match("^(.-)##")or action.label
    local width=math.min(available,math.max(action.width or 0,ui.CalcTextSize(label)+24*unit+2))
    if used>0 and used+gap+width<=available then ui.SameLine();used=used+gap+width
    else used=width end
    if C.button(action.label,width,action.primary,action.enabled,action.height)then action.run()end
    C.tooltip(action.tooltip)
  end
  return used
end
function R.review(title,body,yes,cancel,confirmLabel,cancelLabel)
  C.card("##review:"..title,0,0,function()
    C.section(title);C.note(body,"warn")
    R.actions({{label=confirmLabel,primary=true,enabled=true,height=32*R.s,run=yes},
      {label=cancelLabel or "Cancel",enabled=true,height=32*R.s,run=cancel}})
  end)
end
return R
