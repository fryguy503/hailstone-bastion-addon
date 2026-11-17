local root=ADDON_ROOT or(arg and arg[1])or"."
local setup=assert(loadfile(root.."/tests/received_tools_ui_harness.lua"))()
local checks=0
local function check(ok,why)checks=checks+1;if not ok then error(why,2)end end
for _,pack in ipairs({"modern","gamepad"})do
  local draw,clicks,pick,calls=nil,{},false,{}
  local active,visibilityReads,stateReads=true,0,0
  local s={available=true,shown=false,pending=false,fresh=true,revision="18446744073709551614",message="Received",pet_name="Pet",ownership_known=true,
    locked=false,casting=false,charmed=false,lock_remaining_ms=0,cursor={id="100",name="Cursor",serial="0123456789ABCDEF",instance_id="20",quantity="3"},
    slots={{slot="13",label="Primary",known=true,occupied=true,name="Weapon",id="200",serial="FEDCBA9876543210",instance_id="30",can_equip=true,can_remove=false}}}
  local p={pet_inventory=function(op,...)
    if op=="visibility"then visibilityReads=visibilityReads+1;return{active=active,shown=s.shown}end
    if op=="state"then stateReads=stateReads+1 else calls[#calls+1]={op,...}end
    if op=="close"then s.shown=false end
    return s
  end}
  BASTION_UI={};eq={Plugin=function()return p end};ui={Cond={FirstUseEver=1},window=function(_,fn)draw=fn end,
    SetNextWindowSize=function()end,Begin=function()return true,true end,End=function()end,TextWrapped=function()end,
    BeginChild=function()return true end,EndChild=function()end,SameLine=function()end,
    Button=function(label)return clicks[label]end,Selectable=function()local result=pick;pick=false;return result end}
  local harness=setup(root,pack,ui,eq,check)
  local hovered,release,drag,inspect,press=false,false,false,false,false
  ui.IsItemHovered=function()return hovered end
  ui.IsItemClicked=function(button)return(inspect and button==1)or(press and button==0)end
  ui.IsMouseReleased=function(button)return release and button==0 end
  ui.IsMouseDragging=function(button)return drag and button==0 end
  dofile(root.."/ui/"..pack.."/pet_inventory.lua")
  local function frame()harness.advance();draw();harness.balanced();clicks={};pick=false end
  for _=1,120 do frame()end
  check(visibilityReads==120 and stateReads==0 and #calls==0,"hidden pet inventory requested slots or cursor eligibility")
  BASTION_UI.pet_inventory=true;pick=true;frame();check(#calls==0,"selection action")
  clicks["Inspect item"]=true;frame();check(#calls==1 and calls[1][1]=="inspect"and calls[1][2]==s.revision and calls[1][3]=="13","readonly inspect identity")
  calls={};clicks["Review equip cursor"]=true;frame();check(#calls==0,"review equipped")
  s.cursor.serial="changed";clicks["Yes, change pet equipment"]=true;frame();check(#calls==0,"cursor replacement confirmed")
  s.cursor.serial="0123456789ABCDEF";clicks["Review equip cursor"]=true;frame();clicks["Yes, change pet equipment"]=true;frame()
  check(#calls==1 and calls[1][1]=="equip"and calls[1][2]==s.revision and calls[1][3]=="13"and calls[1][4]=="100"and calls[1][5]==s.cursor.serial and calls[1][6]=="20"and calls[1][7]=="3","exact cursor fingerprint lost")
  calls={};hovered=true;release=true;frame();release=false;hovered=false
  check(#calls==1 and calls[1][1]=="equip"and calls[1][5]==s.cursor.serial,"cursor drop did not equip exact item")
  calls={};hovered=true;inspect=true;frame();inspect=false;hovered=false
  check(#calls==1 and calls[1][1]=="inspect","right click did not inspect pet gear")
  calls={};s.cursor=nil;s.slots[1].can_equip=false;s.slots[1].can_remove=true;hovered=true;drag=true;frame();drag=false;hovered=false
  check(#calls==0,"drag originating outside gear slot removed equipment")
  hovered=true;press=true;frame();press=false;hovered=false;drag=true;frame();drag=false
  check(#calls==1 and calls[1][1]=="remove"and calls[1][5]==s.slots[1].serial,"gear drag did not request authoritative removal")
  calls={};s.slots[1].can_remove=false;hovered=true;drag=true;frame();drag=false;hovered=false
  check(#calls==0,"locked/unowned pet gear dragged to cursor")
  s.cursor={id="100",name="Cursor",serial="0123456789ABCDEF",instance_id="20",quantity="3"};s.slots[1].can_equip=true
  calls={};s.cursor=nil;s.slots[1].can_equip=false;s.slots[1].can_remove=true;clicks["Review remove to cursor"]=true;frame();s.revision="18446744073709551615";clicks["Yes, change pet equipment"]=true;frame();check(#calls==0,"retired revision removed")
  clicks["Review remove to cursor"]=true;frame();clicks["Yes, change pet equipment"]=true;frame();check(#calls==1 and calls[1][1]=="remove"and calls[1][4]=="200"and calls[1][5]==s.slots[1].serial,"remove fingerprint lost")
  calls={};s.slots[1].can_remove=false;s.ownership_known=false;clicks["Review remove to cursor"]=true;clicks["Yes, change pet equipment"]=true;frame();check(#calls==0,"unknown ownership removed")
  s.pending=true;clicks["Refresh pet equipment"]=true;frame();check(#calls==0,"pending duplicate refresh")
  s.pending=false;clicks["Refresh pet equipment"]=true;frame();check(#calls==1 and calls[1][1]=="refresh","manual refresh missing")
  -- Body-slot order remains usable with both actual item icons and missing art.
  local pickedOrder={};local nativeSelectable=ui.Selectable
  ui.Selectable=function(label,...)pickedOrder[#pickedOrder+1]=label;return nativeSelectable(label,...)end
  local ids={1,2,3,5,6,7,8,9,17,12,11,13,14,15,16,20,18,19}
  s.slots={};for _,id in ipairs(ids)do s.slots[#s.slots+1]={slot=tostring(id),label="Slot "..id,known=true,occupied=false,can_equip=false,can_remove=false}end
  s.slots[12].occupied=true;s.slots[12].icon=123;s.slots[12].name="Received weapon"
  eq.ItemIcon=function(icon)check(icon==123,"unreceived icon requested");return "item",0.1,0.2,0.3,0.4 end
  eq.UiAnimation=function(name)return name,0,0,1,1,40,40 end
  harness.images={};frame()
  check(#pickedOrder==18,"equipment grid omitted or invented slots")
  for i,id in ipairs(ids)do check(pickedOrder[i]=="##petslot"..id,"authored body-slot order changed")end
  local item=false;for _,image in ipairs(harness.images)do if image.texture=="item"then item=true;check(image.u0==0.1 and image.v1==0.4,"item crop changed")end end
  check(item,"received item icon not drawn")
  calls={};eq.ItemIcon=function()return nil end;eq.UiAnimation=function()return nil end;frame();check(#calls==0,"missing art performed an action")
  harness.width=1920;harness.height=1080;frame();harness.width=1280;harness.height=720;frame()
  harness.close=true;frame();check(calls[#calls][1]=="close"and not BASTION_UI.pet_inventory,"pack close did not retire equipment review")
  local before=stateReads;for _=1,120 do frame()end
  check(stateReads==before,"closed pet inventory kept reading slots")
  s.shown=true;s.revision="fresh-hidden-pet";s.pet_name="New pet after hidden frames"
  local actions=#calls;frame();check(stateReads==before+1 and #calls==actions,"native pet reopening missed fresh state or sent an action")
  active=false;before=stateReads;frame();check(not BASTION_UI.pet_inventory and stateReads==before,"inactive pet inventory fetched state or retained review visibility")
  active=true;s.shown=false;frame();check(stateReads==before,"reactivated hidden pet inventory fetched state")
  harness.split_layout(dofile("lib/citadel_received.lua"))
  harness.finish()
end
print("Pet inventory UI PASS "..checks.." checks")
