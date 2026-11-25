local Host=dofile("lib/bastion_host.lua")
-- SPDX-License-Identifier: MIT
local R=dofile("lib/citadel_received.lua")
local C=R.C
local Commands=dofile("lib/bastion_commands.lua")
local Integrity=dofile("lib/bastion_integrity.lua")
local MapFilters=dofile("lib/map_filters.lua")
local Twist=dofile("lib/bastion_twist.lua")
local FastCamp=dofile("lib/bastion_fast_camp.lua")
BASTION_UI=BASTION_UI or{}
local lastOpen,page=nil,1
local character={
  {"Enhanced follow","citadel_follow","Distance & movement"},
  {"Faction standings","faction","Standings & export"},
  {"Home city","homecity","Starting city"},
}
local equipment={
  {"Pet gear","petinventory","Inventory & equipment"},
  {"Gear ratings","gear_score","Items & augments"},
}
local adventures={
  {"Raid bars","raidbars","Layout & management"},
  {"Raid Ready Check","raid_ready_check","Responses by subgroup"},
  {"DZ manager","dz","Saved expeditions"},
  {"Encounter telegraphs","telegraphs","Colors & previews"},
  {"Firefell instance","firefell","Current instance"},
}
local function columns(id,left,right)
  local width=ui.GetContentRegionAvail()
  if width<620*R.s then left();ui.Dummy(0,6*R.s);right();return end
  local f=ui.TableFlags or{}
  if ui.BeginTable(id,2,(f.SizingStretchProp or 0))then
    ui.TableSetupColumn("##left",0,1);ui.TableSetupColumn("##right",0,1)
    ui.TableNextRow();ui.TableNextColumn();left();ui.TableNextColumn();right();ui.EndTable()
  end
end
local function launchers(p,entries)
  local width=ui.GetContentRegionAvail();local labelWidth,hintWidth,height=0,0,0
  for _,entry in ipairs(entries)do
    local w,h=ui.CalcTextSize(entry[1]);labelWidth=math.max(labelWidth,w);height=math.max(height,h)
    local hint=ui.CalcTextSize(entry[3]);hintWidth=math.max(hintWidth,hint)
  end
  local buttonWidth=math.min(width,math.max(150*R.s,labelWidth+20*C.scale()))
  local hints=width>=buttonWidth+hintWidth+12*R.s
  for _,entry in ipairs(entries)do
    if C.button(entry[1],hints and buttonWidth or width,false,true,math.max(28*R.s,height+10*C.scale()))then
      if entry[2]=="raid_ready_check"then BASTION_UI.raid_ready_check=true else p.citadel("open",entry[2])end
    end
    C.tooltip(entry[3])
    if hints then ui.SameLine();C.note(entry[3],"muted")end
  end
end
local function toggle(p,state,key,label,enabled)
  ui.BeginDisabled(enabled==false)
  local value,changed=ui.Checkbox(label,(state.settings or{})[key]==1)
  ui.EndDisabled()
  if changed and enabled~=false then p.citadel("set",key,value and 1 or 0)end
end
local corpseModes={"Show corpses","All corpses","All but group","Looted corpses","NPC corpses","Always hide"}
local function corpse_visibility()
  local settings=hs and hs.UI and hs.UI.settings
  if not settings or not settings.Option or not settings.SetOption then return end
  local mode=settings.Option("hide_corpses")
  local label=type(mode)=="number" and corpseModes[mode+1]
  if not label then return end
  C.section("Corpses")
  if ui.BeginCombo("Hide corpses",label)then
    for index,choice in ipairs(corpseModes)do
      local selected=index==mode+1
      if ui.Selectable(choice,selected) and not selected then settings.SetOption("hide_corpses",index-1)end
    end
    ui.EndCombo()
  end
  C.tooltip("Always hide includes your own corpse and corpses that appear later.")
  ui.Dummy(0,6*R.s)
end
local function firefell(p)
  local f=p.firefell and p.firefell("state")
  if not f then return end
  C.section("Firefell instance")
  if f.available then
    C.note(f.ready and f.name~=""and f.name or"Waiting for instance details.",f.ready and"good"or"muted")
    C.field("Instance",tostring(f.instance or"Unavailable").."  |  Version "..tostring(f.ready and f.version or"Waiting"))
    if C.button("Refresh Firefell context",0,false,true)then p.firefell("refresh")end
  else C.note("Enter a Firefell instance to view its details.","muted")end
  ui.Dummy(0,6*R.s)
end
local function settings(p,s)
  columns("bastion-settings",function()
    C.section("Map")
    MapFilters.draw_settings(p,s,C)
    ui.Dummy(0,6*R.s)
    FastCamp.draw(p,R)
  end,function()
    C.section("Merchant")
    toggle(p,s,"right_click_sell","Right-click selling")
    C.note("Off by default. Sells the whole stack while shopping.","muted")
    ui.Dummy(0,6*R.s)
    corpse_visibility()
    Twist.draw(p,R)
    ui.Dummy(0,6*R.s)
    C.section("Tool settings")
    launchers(p,{{"Follow settings","citadel_follow","Distance & movement"},{"Raid bar settings","raidbars","Layout & controls"},{"Telegraph appearance","telegraphs","Colors & previews"}})
  end)
  ui.Dummy(0,8*R.s)
  Integrity.draw(p,C)
end
ui.window("Bastion",function()
  local p=Host.Plugin("bastion");local s=p and p.citadel and p.citadel("visibility")
  MapFilters.sync(p,s)
  if not s or not s.active then Commands.reset();return end
  if lastOpen~=s.open_revision then
    lastOpen=s.open_revision
    if s.panel=="commands"then page=3 elseif s.panel=="settings"then page=2 elseif s.panel=="tools"or s.panel=="general"or s.panel=="firefell"then page=1 end
    if s.panel=="raidbars"or s.panel=="citadel_follow"or s.panel=="telegraphs"or s.panel=="gear_score"then BASTION_UI[s.panel]=true end
  end
  if not s.shown then return end
  s=p.citadel("state")
  if not s or not s.active or not s.shown then return end
  local height=page==3 and 820 or page==2 and 690 or page==1 and s.panel=="firefell"and 640 or 520
  local visible,open=R.begin("Bastion",page==3 and 1100 or 900,height)
  if visible then
    ui.PushStyleVar(ui.StyleVar.ItemSpacing,8*C.scale(),5*C.scale())
    C.header("BASTION",nil,page==1 and (#character+#equipment+#adventures).." tools"or nil)
    page=C.tabs("bastion-main",{"Tools","Settings","Commands"},page)
    if page==1 then
      if s.panel=="firefell"then firefell(p)end
      columns("bastion-tools",function()
        C.section("Character");launchers(p,character)
        ui.Dummy(0,6*R.s);C.section("Equipment");launchers(p,equipment)
      end,function()
        C.section("Raids & adventures");launchers(p,adventures)
      end)
      ui.Dummy(0,8*R.s);ui.Separator()
      if C.button("Map & merchant settings",0,false,true)then page=2 end
      C.note("Kinbound labels and loot warnings appear on items automatically.","muted")
    elseif page==2 then settings(p,s)
    else Commands.draw(p,R)end
    ui.Separator()
    C.note(s.error and s.error~=""and s.error or s.message and s.message~=""and s.message or"Saved for this character.","muted")
    ui.PopStyleVar()
  end
  if R.finish()or not open then p.citadel("close")end
end)
