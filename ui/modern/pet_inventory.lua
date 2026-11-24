local Host=dofile("lib/bastion_host.lua")
local R=dofile("lib/citadel_received.lua")
local C=R.C
BASTION_UI=BASTION_UI or {}
local revision,selected,review,dragSource
-- The authored equipment layout follows the body, rather than wire slot order.
local grid={{1,2,3,5,6},{7,8,9,17,12},{11,13,14,15,16},{0,20,18,19,0}}
local art={[1]="Ear",[2]="Head",[3]="Face",[5]="Neck",[6]="Shoulders",[7]="Arms",[8]="AboutBody",[9]="Wrist",[17]="Chest",[12]="Hands",[11]="Range",[13]="Primary",[14]="Secondary",[15]="Ring",[16]="Ring",[20]="Waist",[18]="Legs",[19]="Feet"}
local function slot(row,w,h,s,p)
  local x,y=ui.GetCursorScreenPos()
  local active=selected==row.slot
  if ui.Selectable("##petslot"..row.slot,active,0,w,h)then selected=row.slot;review=nil end
  local hovered=ui.IsItemHovered()
  if hovered and ui.IsItemClicked and ui.IsItemClicked(0)and not s.cursor and row.can_remove then
    dragSource={revision=s.revision,slot=row.slot,item=row.id,serial=row.serial,instance=row.instance_id}
  end
  if hovered and ui.IsItemClicked and ui.IsItemClicked(1)and row.occupied then
    p.pet_inventory("inspect",s.revision,row.slot)
  end
  -- Hailstone's inventory drag carries its real cursor item. Release over a
  -- pet slot submits that exact item; it is never moved locally on send.
  if hovered and s.cursor and row.can_equip and not s.pending and s.fresh and
    ui.IsMouseReleased and ui.IsMouseReleased(0)then
    p.pet_inventory("equip",s.revision,row.slot,s.cursor.id,s.cursor.serial,s.cursor.instance_id,s.cursor.quantity)
    selected=row.slot;review=nil
  end
  C.tooltip(row.label.."\n"..(not row.known and"Not yet received"or(row.occupied and row.name or"Empty")))
  local c=active and C.colors.gold or C.colors.border
  ui.DrawRect(x+1,y+1,x+w-1,y+h-1,c[1],c[2],c[3],1,4*R.s,active and 2 or 1)
  local texture,u0,v0,u1,v1
  if row.occupied and row.icon and Host.ItemIcon then texture,u0,v0,u1,v1=Host.ItemIcon(row.icon)
  elseif Host.UiAnimation then texture,u0,v0,u1,v1=Host.UiAnimation("A_Inv"..(art[tonumber(row.slot)]or""))end
  local size=math.min(math.max(32,42*R.s),w-12*R.s,h-25*R.s)
  if texture then ui.DrawImage(texture,x+(w-size)/2,y+5*R.s,x+(w+size)/2,y+5*R.s+size,u0 or 0,v0 or 0,u1 or 1,v1 or 1)end
  local label=row.label;local tw=ui.CalcTextSize(label)
  local t=row.known and C.colors.text or C.colors.muted
  ui.PushClipRect(x+2,y+2,x+w-2,y+h-2,true)
  ui.DrawText(x+math.max(4*R.s,(w-tw)/2),y+h-21*R.s,t[1],t[2],t[3],t[4],label)
  ui.PopClipRect()
end
ui.window("Bastion Pet Inventory",function()
  local p=Host.Plugin("bastion")
  local visibility=p and p.pet_inventory and p.pet_inventory("visibility")
  if not visibility or visibility.active~=true then
    BASTION_UI.pet_inventory=false;revision=nil;selected=nil;review=nil;dragSource=nil
    return
  end
  if not visibility.shown and not BASTION_UI.pet_inventory then return end
  local s=p.pet_inventory("state");if not s then return end
  if revision~=s.revision then revision=s.revision;review=nil;dragSource=nil end
  if not s.shown and not BASTION_UI.pet_inventory then return end
  local visible,open=R.begin("Bastion Pet Inventory",1050,720);BASTION_UI.pet_inventory=open
  if not open then p.pet_inventory("close");review=nil end
  if visible then
    C.header("Pet Equipment",s.pet_name~=""and s.pet_name or"No pet",s.pending and"Updating"or
      (not s.fresh and"Awaiting status"or not s.managed_pet and"Unavailable"or
       s.casting and"Casting"or s.locked and"Removal cooldown"or"Ready"))
    if s.casting then C.note("Finish casting before changing equipment.","warn")end
    if s.locked then C.gauge("Removal cooldown",nil,math.ceil((s.lock_remaining_ms or 0)/1000).." seconds  |  Equipping remains available","warn",22*R.s)end
    local rows={};for _,v in ipairs(s.slots or {})do rows[tonumber(v.slot)]=v end
    local row;for _,v in ipairs(s.slots or {})do if v.slot==selected then row=v end end
    if not row then review=nil end
    -- Gamepad's design scale shrinks at720p; keep physical room for five
    -- readable slot labels and the review footer rather than squeezing them.
    R.split("pet-equipment",math.max(400,450*R.s),R.height_for(math.max(450,380/R.s),120,150),function()
      C.section("Equipment slots","Select a slot to inspect or change its equipment.")
      if ui.BeginTable("pet-body-grid",5,0)then
        for _,line in ipairs(grid)do
          ui.TableNextRow()
          for _,id in ipairs(line)do
            R.cell();local width=ui.GetContentRegionAvail()
            if rows[id]then slot(rows[id],math.max(30*R.s,width),math.max(60,72*R.s),s,p)else ui.Dummy(0,math.max(60,72*R.s))end
          end
        end
        ui.EndTable()
      end
    end,function()
      row=nil;for _,v in ipairs(s.slots or {})do if v.slot==selected then row=v;break end end
      C.section("Selected equipment")
      if row then
        if row.occupied then R.icon(row.icon,48*R.s);ui.SameLine()end
        C.note(row.label,"gold")
        C.note(row.occupied and row.name or(row.known and"Empty slot"or"Equipment not yet received"))
        if row.occupied then
          C.field("Quantity",row.quantity or"1")
          C.note(row.managed_known and(row.managed and"This item can be returned to your cursor."or"This item cannot be removed.")or"Refresh to check whether this item can be removed.","muted")
          if C.button("Inspect item",0,false,true)then p.pet_inventory("inspect",s.revision,row.slot)end
        end
      else C.note("Choose a slot on the left.","muted")end
      ui.Separator();C.section("Your cursor")
      C.note(s.cursor and(s.cursor.name.." x"..s.cursor.quantity)or"Empty cursor",s.cursor and"text"or"muted")
       C.note("Drop your cursor item on a pet slot to equip it. Drag removable pet gear to your empty cursor; wait for the item to arrive before dropping it in inventory. Right-click gear to inspect.","muted")
      if row then
        if C.button("Review equip cursor",0,true,row.can_equip and s.cursor~=nil)then
          review={op="equip",revision=s.revision,slot=row.slot,item=s.cursor.id,serial=s.cursor.serial,instance=s.cursor.instance_id,quantity=s.cursor.quantity,name=s.cursor.name}
        end
        if C.button("Review remove to cursor",0,"danger",row.can_remove)then
          review={op="remove",revision=s.revision,slot=row.slot,item=row.id,serial=row.serial,instance=row.instance_id,name=row.name}
        end
      end
    end)
    if dragSource then
      local same=dragSource.revision==s.revision and not s.cursor and not s.pending and s.fresh
      local source;for _,v in ipairs(s.slots or{})do if v.slot==dragSource.slot then source=v;break end end
      same=same and source and source.can_remove and source.id==dragSource.item and source.serial==dragSource.serial and source.instance_id==dragSource.instance
      if same and ui.IsMouseDragging and ui.IsMouseDragging(0)then
        p.pet_inventory("remove",dragSource.revision,dragSource.slot,dragSource.item,dragSource.serial,dragSource.instance)
        selected=dragSource.slot;review=nil;dragSource=nil
      elseif not same or(ui.IsMouseReleased and ui.IsMouseReleased(0))then dragSource=nil end
    end
    if review then
      local same=row and review.revision==s.revision and review.slot==row.slot and not s.pending and s.fresh
      if review.op=="equip"then same=same and row.can_equip and s.cursor and s.cursor.id==review.item and s.cursor.serial==review.serial and s.cursor.instance_id==review.instance and s.cursor.quantity==review.quantity
      else same=same and row.can_remove and row.id==review.item and row.serial==review.serial and row.instance_id==review.instance end
      if not same then review=nil end
    end
    if review then
      R.review("Confirm equipment change",(review.op=="equip"and"Equip "or"Remove ")..review.name.." in "..row.label.."?",function()
        p.pet_inventory(review.op,review.revision,review.slot,review.item,review.serial,review.instance,review.quantity);review=nil
      end,function()review=nil end,"Yes, change pet equipment","Cancel pet change")
    else
      ui.Separator();R.status(s,"Pet equipment updated.")
      if C.button("Refresh pet equipment",0,true,s.available and not s.pending)then p.pet_inventory("refresh");review=nil end
    end
  end
  if R.finish()then p.pet_inventory("close");BASTION_UI.pet_inventory=false;review=nil end
end)
