local Host=dofile("lib/bastion_host.lua")
local R=dofile("lib/citadel_received.lua")
local C=R.C
BASTION_UI=BASTION_UI or {}
local revision,rows,categories=nil,{},{}
local commandSearch,dirty=nil,true
local query,category,page,selected="",0,1,nil
local filtered,filterKey={},nil
local sort,ascending="name",true
local collapsed={}
local reactionColor={"good","good","good","blue","muted","warn","warn","danger","danger"}
local function rebuild()
  local chosen={[category]=true}
  if category~=0 then
    for _=1,#categories do
      local changed=false
      for _,c in ipairs(categories)do if chosen[c.parent]and not chosen[c.id]then chosen[c.id]=true;changed=true end end
      if not changed then break end
    end
  end
  filtered={};local needle=query:lower()
  for _,e in ipairs(rows)do
    -- A submitted name/ID search spans the complete authoritative catalog,
    -- including hidden later-expansion categories.
    local included=needle~=""or category==-1
    for _,id in ipairs(e.categories or {})do if chosen[id]then included=true;break end end
    if included and(needle==""or e.name:lower():find(needle,1,true)or tostring(e.id):find(needle,1,true))then filtered[#filtered+1]=e end
  end
  table.sort(filtered,function(a,b)
    local av,bv
    if sort=="standing"then av,bv=9-a.reaction,9-b.reaction elseif sort=="gauge"then av,bv=a.gauge,b.gauge else av,bv=a.name:lower(),b.name:lower()end
    if av==bv then
      if a.name:lower()~=b.name:lower()then return a.name:lower()<b.name:lower()end
      return a.id<b.id
    end
    if ascending then return av<bv else return av>bv end
  end)
end
ui.window("Bastion Factions",function()
  local p=Host.Plugin("bastion")
  local visibility=p and p.factions and p.factions("visibility")
  if not visibility or visibility.active~=true then
    BASTION_UI.factions=false;revision=nil;rows={};categories={};commandSearch=nil;dirty=true
    query="";category=0;page=1;selected=nil;filtered={};filterKey=nil;collapsed={}
    return
  end
  if not visibility.shown and not BASTION_UI.factions then return end
  local s=p.factions("state",revision);if not s then return end
  if s.search_revision and commandSearch~=s.search_revision then
    commandSearch=s.search_revision;query=s.search or"";page=1;filterKey=nil
  end
  if not s.unchanged then
    revision=s.revision;rows=s.entries or {};categories=s.categories or {};dirty=true
    local shown={}
    for _,c in ipairs(categories)do if c.parent==0 and(c.order<=11 or c.id==4294967295)then shown[c.id]=true end end
    for _=1,#categories do
      local changed=false
      for _,c in ipairs(categories)do if c.parent~=0 and shown[c.parent]and not shown[c.id]then shown[c.id]=true;changed=true end end
      if not changed then break end
    end
    local visible={};for _,c in ipairs(categories)do if shown[c.id]then visible[#visible+1]=c end end;categories=visible
    table.sort(categories,function(a,b)
      if(a.id==4294967295)~=(b.id==4294967295)then return b.id==4294967295 end
      if a.order~=b.order then return a.order<b.order end
      if a.name~=b.name then return a.name<b.name end
      return a.id<b.id
    end)
    local found=category==0 or category==-1;for _,c in ipairs(categories)do if c.id==category then found=true end end
    if not found then category=0 end
    if selected then local exists=false;for _,e in ipairs(rows)do if e.id==selected then exists=true;break end end;if not exists then selected=nil end end
  end
  if not s.shown and not BASTION_UI.factions then return end
  local visible,open=R.begin("Bastion Factions",1120,740);BASTION_UI.factions=open
  if not open then p.factions("close")end
  if visible then
    C.header("Faction Standings","Explore your relationships across Norrath.",#rows.." factions")
    C.note("Search by faction name or ID","muted");ui.SetNextItemWidth(-1)
    query=ui.InputText("##faction-search",query,96)
    local height=R.height_for(360,250)
    R.split("factions-browser",230*R.s,height,function()
      C.section("Categories")
      if ui.Selectable("All factions",category==-1)then category=-1;page=1 end
      local children={};for _,c in ipairs(categories)do children[c.parent]=children[c.parent]or {};table.insert(children[c.parent],c)end
      local function branch(parent,depth)
        if depth>128 then return end
        for _,c in ipairs(children[parent]or {})do
          if depth>0 then ui.Indent(12*R.s)end
          if children[c.id]then
            if ui.SmallButton((collapsed[c.id]and "+"or"-").."##branch"..c.id)then collapsed[c.id]=not collapsed[c.id]end
            ui.SameLine()
          end
          if R.category(c,category==c.id)then category=c.id;page=1 end
          if not collapsed[c.id]then branch(c.id,depth+1)end
          if depth>0 then ui.Unindent(12*R.s)end
        end
      end
      branch(0,0)
    end,function()
      local key=category..":"..query..":"..sort..":"..tostring(ascending)
      if key~=filterKey then filterKey=key;rebuild();page=1;dirty=false
      elseif dirty then rebuild();dirty=false end
      local pages=math.max(1,math.ceil(#filtered/100));page=math.max(1,math.min(page,pages))
      C.field("Results",#filtered.." factions  |  "..page.." / "..pages)
      R.table("faction-standings",{{"Faction",3},{"Standing",1.1},{"Gauge",1.3}},function()
        for i=(page-1)*100+1,math.min(page*100,#filtered)do
          local e=filtered[i];ui.TableNextRow();R.cell()
          if ui.Selectable(e.name.."##faction"..e.id,selected==e.id)then selected=e.id end
          C.tooltip(e.name.."\nFaction "..e.id)
          R.cell();C.note(e.standing,reactionColor[e.reaction]or"muted")
          R.cell();C.gauge(nil,math.max(0,math.min(1000,e.gauge))/1000,tostring(e.earned),reactionColor[e.reaction],18*R.s)
        end
      end)
      if #filtered==0 then C.note(category==0 and query==""and"Choose a category or search to display factions."or"No factions match this search.","muted")end
    end)
    local available=ui.GetContentRegionAvail();local unit=math.max(R.s,C.scale());local gap=10*unit+1
    local used=R.actions({{label="Previous factions",enabled=page>1,run=function()page=page-1 end},
      {label="Next factions",enabled=page<math.ceil(#filtered/100),run=function()page=page+1 end}})
    local direction=ascending and"Ascending"or"Descending"
    local directionWidth=math.min(available,ui.CalcTextSize(direction)+24*unit+2)
    local labelWidth=ui.CalcTextSize("Sort by")+8*unit
    local comboWidth=math.min(200*unit,math.max(1,available-directionWidth-labelWidth-gap))
    local together=comboWidth>=80*unit
    if not together then comboWidth=math.max(1,math.min(200*unit,available-labelWidth))end
    local sortWidth=comboWidth+labelWidth+(together and gap+directionWidth or 0)
    if together and used+gap+sortWidth<=available then ui.SameLine()end
    ui.SetNextItemWidth(comboWidth)
    if ui.BeginCombo("Sort by",sort=="name"and"Faction name"or(sort=="standing"and"Standing"or"Gauge"))then
      for _,v in ipairs({{"name","Faction name"},{"standing","Standing"},{"gauge","Gauge"}})do if ui.Selectable(v[2],sort==v[1])then sort=v[1]end end
      ui.EndCombo()
    end
    if together then ui.SameLine()end
    if C.button(direction,directionWidth,false,true)then ascending=not ascending end
    local row;for _,e in ipairs(filtered)do if e.id==selected then row=e;break end end
    if row then
      C.section(row.name,"Faction "..row.id)
      C.gauge(row.standing,math.max(0,math.min(1000,row.gauge))/1000,"Earned "..row.earned.."  |  Effective "..row.effective,reactionColor[row.reaction],26*R.s)
      if row.reaction~=1 then C.note(math.max(0,row.next_threshold-row.effective).." points to the next standing.","muted")else C.note("Highest standing reached.","good")end
    else C.note("Select a faction to view its standing and progress.","muted")end
    ui.Separator();R.status(s,"Standings updated.")
    R.actions({
      {label="Refresh standings",primary=true,enabled=s.available and not s.pending,run=function()p.factions("refresh")end},
      {label="Export file",enabled=s.fresh and not s.pending,tooltip="Save all received standings to a text file.",run=function()p.factions("export")end},
      {label="Copy TSV",enabled=s.fresh and not s.pending and ui.SetClipboardText~=nil,tooltip="Copy all received standings as tab-separated text.",run=function()
        local export={"server_faction_id\tname\tearned\teffective\tminimum\tmaximum\tnext_threshold\treaction\tcategory_ids"}
        for _,e in ipairs(rows)do export[#export+1]=table.concat({e.id,e.name,e.earned,e.effective,e.minimum,e.maximum,e.next_threshold,e.reaction,table.concat(e.categories or {},",")},"\t")end
        ui.SetClipboardText(table.concat(export,"\n"))
      end},
    })
    if s.message and s.message:find("Exported ",1,true)then C.note(s.message,"good")end
  end
  if R.finish()then p.factions("close");BASTION_UI.factions=false end
end)
