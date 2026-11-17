-- SPDX-License-Identifier: MIT
local R=dofile("lib/citadel_received.lua")
local C=R.C
local M={}
local localCommands={
  {name="/bastion",help="Open the Bastion hub. /bastion tools | settings | commands selects a page."},
  {name="/citadel",help="Alias for /bastion. /cit also opens the same hub."},
  {name="/follow",help="Follow your target. /follow on | off | distance <1-50>; /follow citadel on | off; /follow citadel cancel on | off.",tool="citadel_follow"},
  {name="/raidbars",help="Open enhanced raid bars. /raidbars help lists display and member controls.",tool="raidbars"},
  {name="/faction",help="Toggle faction standings. /faction <partial name> searches the complete faction catalog.",tool="faction"},
  {name="/outputfile faction",help="Export all fresh faction standings. Optional quoted filename: /outputfile faction \"My Factions\". Files go to the addon settings exports folder."},
  {name="/dzmanager",help="Open saved expeditions. /dzmanager add | remove | list [name] manages delegates; saved and resume preserve the server shortcuts.",tool="dz"},
  {name="/petinventory",help="Toggle pet equipment. Drag a cursor item onto a slot; removable equipment returns to your cursor. /petgear is an alias.",tool="petinventory"},
  {name="/petgear",help="Alias for /petinventory.",tool="petinventory"},
  {name="/setstartcity",help="Open home city choices. /setstartcity [city ID] preselects a returned choice; review and confirm before using the grant.",tool="homecity"},
  {name="/gearratings",help="Open server item ratings and equipped/best-owned summaries.",tool="gear_score"},
  {name="/telegraphs",help="Open encounter appearance and previews. status | on | off | opacity <0-100> | color <role> <AARRGGBB> | reload | hello | clear | selftest | test <shape|all|palette>.",tool="telegraphs"},
  {name="/firefell",help="Show the current Firefell instance name, version and received context.",tool="firefell"},
  {name="/rightclicksell",help="on | off | toggle. Disabled by default; a merchant right-click sells the carried item's whole stack. Bags retain ordinary behavior."},
  {name="/bastion maps",help="raid | mobs | merchants | players | corpses on | off. NPCs require trained Tracking and Citadel's range."},
  {name="/kinbound",help="Forward #kinbound to Bastion. Put the item on your cursor and follow the server's attunement confirmation."},
}
local catalog,query,staff,category,page,selected,arguments,prepared,message=nil,"",false,"All",1,nil,"",nil,""
local pageSize=16
function M.reset()catalog=nil;query="";staff=false;category="All";page=1;selected=nil;arguments="";prepared=nil;message=""end
local function commandLine(row)return(row.local_command and row.name or"#"..row.name)..(arguments~=""and" "..arguments or"")end
local function validArguments(value)return #value<=768 and not value:find("[%z\1-\31\127]")end
function M.draw(p,frame)
  R.s=frame and frame.s or C.scale()
  if not catalog then
    local result=p.commands and p.commands("catalog")
    if not result then C.note("The command catalog is unavailable.","warn");return end
    catalog={};for _,row in ipairs(result.commands or{})do catalog[#catalog+1]=row end
    for _,source in ipairs(localCommands)do
      local row={};for k,v in pairs(source)do row[k]=v end
      row.local_command=true;row.category="Addon";row.access="Player";row.shortcut="";catalog[#catalog+1]=row
    end
    table.sort(catalog,function(a,b)return a.name:lower()<b.name:lower()end)
  end
  local oldQuery=query;query=ui.InputText("Search commands and help",query,128)
  local oldStaff=staff;staff=select(1,ui.Checkbox("Include staff commands",staff))
  local oldCategory=category
  if ui.BeginCombo("Command category",category)then
    for _,value in ipairs({"All","Addon","Bastion","Server"})do if ui.Selectable(value,category==value)then category=value end end
    ui.EndCombo()
  end
  if oldQuery~=query or oldStaff~=staff or oldCategory~=category then page=1;prepared=nil end
  local rows,serverCount={},0
  for _,row in ipairs(catalog)do
    if not row.local_command then serverCount=serverCount+1 end
    local haystack=(row.name.." "..(row.help or"").." "..(row.shortcut or"").." "..(row.access or"").." "..(row.category or"")):lower()
    if(staff or row.access=="Player")and(category=="All"or row.category==category)and(query==""or haystack:find(query:lower(),1,true))then rows[#rows+1]=row end
  end
  local pages=math.max(1,math.ceil(#rows/pageSize));page=math.max(1,math.min(page,pages))
  C.field("Command library",serverCount.." server  |  "..#localCommands.." addon  |  "..#rows.." matching  |  page "..page.." / "..pages)
  C.card("bastion-command-list",0,math.min(320*R.s,R.height_for(320,220,120)),function()
    R.table("bastion-commands",{{"Command",2},{"Access",1},{"Category",1}},function()
      for i=(page-1)*pageSize+1,math.min(page*pageSize,#rows)do
        local row=rows[i];ui.TableNextRow();R.cell()
        local label=(row.local_command and row.name or"#"..row.name).."##command:"..row.name
        if ui.Selectable(label,selected==row.name)then selected=row.name;arguments="";prepared=nil;message=""end
        C.tooltip(row.help);R.cell();C.note(row.access or"Unknown",row.access=="Player"and"muted"or"warn")
        R.cell();C.note(row.category or"Server","muted")
      end
    end)
    if #rows==0 then C.note("No commands match these filters.","muted")end
  end)
  if C.button("Previous commands",0,false,page>1)then page=page-1 end
  ui.SameLine();if C.button("Next commands",0,false,page<pages)then page=page+1 end
  local row;for _,entry in ipairs(rows)do if entry.name==selected then row=entry;break end end
  if not row then prepared=nil;C.note("Select a command to read its complete help and prepare arguments.","muted");return end
  C.section(row.local_command and row.name or"#"..row.name,row.help)
  C.field("Required access",row.access or"Unknown",row.access=="Player"and"muted"or"warn")
  if row.shortcut and row.shortcut~=""then C.field("Shortcut",row.shortcut)end
  local oldArguments=arguments;arguments=ui.InputText("Command arguments",arguments,768)
  if oldArguments~=arguments then prepared=nil end
  local line=commandLine(row);C.note(line,"blue")
  if C.button("Copy command",0,false,ui.SetClipboardText~=nil)then ui.SetClipboardText(line)end
  if row.local_command then
    if row.tool and row.tool~=""then ui.SameLine();if C.button("Open command tool",0,true)then p.citadel("open",row.tool)end end
    C.note("Copy this local command into chat, or open its tool.","muted")
  elseif prepared then
    if prepared.name~=row.name or prepared.arguments~=arguments then prepared=nil
    else
      R.review("Send "..line.."?","This runs the selected server command with the arguments shown above.",function()
        local result=p.commands("send",prepared.name,prepared.arguments)
        message=result and result.ok and"Command sent. Read the server response in chat."or"The command could not be sent."
        prepared=nil
      end,function()prepared=nil end,"Send command","Cancel command")
    end
  else
    ui.SameLine();if C.button("Prepare command",0,true,validArguments(arguments))then prepared={name=row.name,arguments=arguments}end
  end
  if not validArguments(arguments)then C.note("Arguments must fit on one line.","warn")end
  if message~=""then C.note(message,"muted")end
end
return M
