local Host=dofile("lib/bastion_host.lua")
local R=dofile("lib/citadel_received.lua")
local C=R.C
BASTION_UI=BASTION_UI or {}
local revision,selected,review,commandSelection
-- Authored atlas landmarks, in its 635x382 presentation space. These only
-- locate received choices; neither geography nor art grants eligibility.
local pins={}
for _,group in ipairs({{{1,2,45},163,151},{{3},176,115},{{8,9,10},340,143},{{19},259,125},{{23,24},83,148},{{29},231,47},{{40,41,42},334,108},{{49},193,240},{{50},211,207},{{52},244,276},{{54},492,130},{{55},566,182},{{60,67},449,124},{{61,62},516,155},{{75},64,186},{{82,106},518,268}})do
  for _,id in ipairs(group[1])do pins[id]={group[2],group[3]}end
end
local tiles={{0,0,255,255},{255,0,254,255},{509,0,126,255},{0,255,255,127},{255,255,254,127},{509,255,126,127}}
local races={[1]="Human",[2]="Barbarian",[3]="Erudite",[4]="Wood Elf",[5]="High Elf",[6]="Dark Elf",[7]="Half Elf",[8]="Dwarf",[9]="Troll",[10]="Ogre",[11]="Halfling",[12]="Gnome",[128]="Iksar",[130]="Vah Shir",[330]="Froglok",[522]="Drakkin"}
local classes={"Warrior","Cleric","Paladin","Ranger","Shadow Knight","Druid","Monk","Bard","Rogue","Shaman","Necromancer","Wizard","Magician","Enchanter","Beastlord","Berserker"}
local function map(s,compact)
  if compact then
    C.section("Choose your home city")
    C.note("Choose a city below. Your ordinary bind stays unchanged.","muted")
    return
  end
  local images={};local complete=Host.UiAnimation~=nil
  if complete then for i=1,6 do images[i]={Host.UiAnimation(string.format("A_StartingCityMap%02d",i))};if not images[i][1]then complete=false end end end
  if not complete then
    C.section("Choose your home city")
    C.note("Select an available destination from the city list.","muted")
    C.note("This free reset changes where you return home. Your ordinary bind stays where it is.")
    return
  end
  local width,available=ui.GetContentRegionAvail();local scale=math.min(width/635,math.max(40,available-28*R.s)/382)
  local x,y=ui.GetCursorScreenPos();local w,h=635*scale,382*scale
  ui.Dummy(w,h);local ex,ey=ui.GetCursorScreenPos()
  for i,tile in ipairs(tiles)do
    local a=images[i];ui.DrawImage(a[1],x+tile[1]*scale,y+tile[2]*scale,x+(tile[1]+tile[3])*scale,y+(tile[2]+tile[4])*scale,a[2],a[3],a[4],a[5])
  end
  for _,entry in ipairs(s.entries or {})do
    local point=pins[entry.zone_id]
    if point then
      local px,py=x+point[1]*scale,y+point[2]*scale
      local c=entry.choice==selected and C.colors.gold or(entry.can_choose and C.colors.good or C.colors.muted)
      ui.DrawCircleFilled(px,py,5*R.s,c[1],c[2],c[3],1)
      ui.DrawCircle(px,py,8*R.s,c[1],c[2],c[3],1,1)
      ui.SetCursorScreenPos(px-10*R.s,py-10*R.s)
      if ui.InvisibleButton("##home-map"..entry.choice,20*R.s,20*R.s)then selected=entry.choice;review=nil end
      C.tooltip(entry.name..(entry.can_choose and""or" | Current home"))
    end
  end
  ui.SetCursorScreenPos(ex,ey);C.note("Select a city on the map or in the list.","muted")
end
ui.window("Bastion Home City",function()
  local p=Host.Plugin("bastion")
  local visibility=p and p.home_city and p.home_city("visibility")
  if not visibility or visibility.active~=true then
    BASTION_UI.home_city=false;revision=nil;selected=nil;review=nil;commandSelection=nil
    return
  end
  if not visibility.shown and not BASTION_UI.home_city then return end
  local s=p.home_city("state");if not s then return end
  local validGrant=s.available==true and s.fresh==true and type(s.grant)=="string"and s.grant:match("^%d+$")~=nil and s.grant:find("[1-9]")~=nil and type(s.snapshot)=="string"and s.snapshot:match("^%d+$")~=nil and s.snapshot:find("[1-9]")~=nil
  if revision~=s.revision then
    revision=s.revision;review=nil
    local exists=false;for _,entry in ipairs(s.entries or{})do if entry.choice==selected then exists=true;break end end
    if not exists then selected=nil end
  end
  if s.selection_revision and commandSelection~=s.selection_revision then
    commandSelection=s.selection_revision;selected=nil;review=nil
    for _,entry in ipairs(s.entries or{})do if entry.choice==s.preselected_choice then selected=entry.choice;break end end
  end
  if not s.shown and not BASTION_UI.home_city then return end
  local visible,open=R.begin("Bastion Home City",1160,750);BASTION_UI.home_city=open
  if not open then p.home_city("close");review=nil end
  if visible then
    C.header("Choose Your Home City","A fresh beginning, without changing your ordinary bind.",validGrant and"Free reset"or"Home City")
    local character=(races[s.race]or("Race "..s.race)).." | "..(classes[s.class_id]or("Class "..s.class_id))
    local current="Zone "..s.current_home;for _,entry in ipairs(s.entries or {})do if entry.zone_id==s.current_home then current=entry.name;break end end
    local grant=not validGrant and"Reset not confirmed"or(s.suppressed and"Reminders disabled"or"Free reset available")
    local grantColor=validGrant and not s.suppressed and"good"or"muted"
    if R.narrow()then
      C.note("Character: "..character);C.note("Home: "..current,"gold");C.note("Reset: "..grant,grantColor)
    else
      R.table("home-profile",{{"Character",2},{"Current home",2},{"Grant",1.5}},function()
        ui.TableNextRow();R.cell();R.icon(nil,40*R.s,"HC_A_Race"..s.race);ui.SameLine();R.icon(nil,40*R.s,"HC_A_Class"..s.class_id);ui.SameLine();C.note(character)
        R.cell();C.note(current,"gold");R.cell();C.note(grant,grantColor)
      end)
    end
    R.split("home-destination",660*R.s,R.height_for(470,190,review and 190 or 150),function(compact)map(s,compact)end,function()
      C.section("Available cities",#(s.entries or {}).." destinations")
      if ui.BeginChild("home-choices",0,0,false)then
        for _,entry in ipairs(s.entries or {})do
          if ui.Selectable(entry.name.." (zone "..entry.zone_id..")##home"..entry.choice,selected==entry.choice)then selected=entry.choice;review=nil end
          if not entry.can_choose then C.note("Current home","muted")end
        end
        if #(s.entries or {})==0 then C.note(s.pending and"Updating destinations..."or"Refresh to view available destinations.","muted")end
      end
      ui.EndChild()
    end,110*math.max(R.s,C.scale()))
    local row;for _,entry in ipairs(s.entries or {})do if entry.choice==selected then row=entry;break end end
    C.section(row and row.name or"Your destination")
    C.note(row and(row.can_choose and"Choosing this city uses your free reset."or"This is already your home city.")or"Select a city to review the change.","muted")
    if s.message and(s.message:find("Home city ID ",1,true)or s.message:find("requested home city",1,true))then C.note(s.message,"warn")end
    if review and(review.revision~=s.revision or review.grant~=s.grant or review.snapshot~=s.snapshot or not validGrant or s.pending or(review.op=="commit"and(not row or not row.can_choose or row.choice~=review.choice)))then review=nil end
    if review then
      R.review(review.op=="commit"and"Confirm your new home"or"Disable reminders?",review.op=="commit"and("Use your free reset to make "..review.name.." your home city? Your ordinary bind will stay unchanged.")or"Stop automatic reminders for this free reset. You can still open Home City whenever you want.",function()
        p.home_city(review.op,review.revision,review.grant,review.snapshot,review.choice);review=nil
      end,function()review=nil end,"Yes, confirm home choice","Cancel home choice")
    else
      ui.Separator();R.status(s,"Select a city, then review your free reset.")
      R.actions({
        {label="Review home city change",primary=true,enabled=row and row.can_choose and validGrant and not s.pending,
          run=function()review={op="commit",revision=s.revision,grant=s.grant,snapshot=s.snapshot,choice=row.choice,name=row.name}end},
        {label="Later",enabled=true,run=function()p.home_city("close");BASTION_UI.home_city=false;review=nil end},
        {label="Refresh home city choices",enabled=s.available and not s.pending,run=function()p.home_city("refresh");review=nil end},
      })
      if C.button("Review disable reminders",0,false,validGrant and not s.pending and not s.suppressed)then review={op="suppress",revision=s.revision,grant=s.grant,snapshot=s.snapshot}end
    end
  end
  if R.finish()then p.home_city("close");BASTION_UI.home_city=false;review=nil end
end)
