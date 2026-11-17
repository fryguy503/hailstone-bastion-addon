local root=ADDON_ROOT or (arg and arg[1])or "."
local setup=assert(loadfile(root.."/tests/received_tools_ui_harness.lua"))()
local checks=0
local function check(ok,why)checks=checks+1;if not ok then error(why,2)end end
for _,pack in ipairs({"modern","gamepad"})do
  local draw,clicks,pick,needle,calls=nil,{},nil,nil,{}
  local lines,begins,ends,options,clipboard={},0,0,0,nil
  local harness,sortWidth
  local active,visibilityReads,stateReads=true,0,0
  local s={available=true,shown=false,pending=false,fresh=true,revision="18446744073709551614",message="Received",
    categories={{id=1,parent=0,order=1,name="Parent",icon=""},{id=2,parent=1,order=2,name="Child",icon=""},
      {id=3,parent=0,order=12,name="Hidden Expansion",icon=""},{id=4,parent=3,order=1,name="Hidden Child",icon=""}},entries={}}
  for i=1,150 do s.entries[i]={id=100+i,name="Faction "..i,standing="Indifferent",earned=i,effective=i+1,minimum=0,maximum=200,next_threshold=100,reaction=5,gauge=i*5,categories={i==150 and 2 or 1}}end
  local p={factions=function(op,cached)
    if op=="visibility"then visibilityReads=visibilityReads+1;return{active=active,shown=s.shown}end
    if op=="state"then stateReads=stateReads+1 end
    if op=="close"then s.shown=false end
    if op~="state"then calls[#calls+1]=op end
    local result={};for k,v in pairs(s)do result[k]=v end
    if cached==s.revision then result.unchanged=true;result.entries=nil;result.categories=nil end
    return result
  end}
  BASTION_UI={};eq={Plugin=function()return p end};ui={Cond={FirstUseEver=1},window=function(_,fn)draw=fn end,
    SetNextWindowSize=function()end,Begin=function()return true,true end,End=function()end,TextWrapped=function(v)lines[#lines+1]=v end,
    BeginChild=function()begins=begins+1;return true end,EndChild=function()ends=ends+1 end,SameLine=function()end,
    Button=function(label,width)
      if label=="Ascending"or label=="Descending"then
        local available=ui.GetContentRegionAvail()
        check(type(width)=="number"and width>0 and sortWidth+ui.CalcTextSize("Sort by")+19+width<=available+0.1,
          "bounded sort field pushed direction beyond the available row")
      end
      return clicks[label]
    end,
    InputText=function(label,old)check(label=="##faction-search"and harness.nextItemWidth==-1,"search input retained a clipped inline label");return needle or old end,
    BeginCombo=function(label)check(label=="Sort by"and harness.nextItemWidth>0,"sort combo width was unbounded");sortWidth=harness.nextItemWidth;return true end,EndCombo=function()end,
    Selectable=function(label)check(not label:find("Hidden",1,true),"later expansion category rendered");if label:find("##faction",1,true)then options=options+1 end;return pick and label:find(pick,1,true)~=nil end,
    ProgressBar=function(fraction)check(fraction>=0 and fraction<=1,"unbounded gauge")end}
  ui.SetClipboardText=function(value)clipboard=value end
  harness=setup(root,pack,ui,eq,check)
  dofile(root.."/ui/"..pack.."/factions.lua")
  local function frame()harness.advance();options=0;lines={};draw();harness.balanced();clicks={};pick=nil;needle=nil end
  for _=1,120 do frame()end
  check(visibilityReads==120 and stateReads==0 and #calls==0 and options==0,"hidden window requested full faction state")
  BASTION_UI.factions=true;frame();check(options==0 and #calls==0,"initial view selected a category")
  pick="All factions";frame();frame();check(options==100 and #calls==0,"page budget/default passive")
  clicks["Next factions"]=true;frame();frame();check(options==50,"page two bound")
  s.revision="changed";frame();check(options==50,"background snapshot reset page")
  pick="Child##category2";frame();check(options==1,"child category filter")
  needle="Faction 1";frame();check(options>1,"name search stayed constrained to child category")
  needle="";pick="Parent##category1";frame()
  pick="Parent##category1";frame();check(options==100,"ancestor category lost child records")
  needle="Faction 150";frame();check(options==1,"literal name search")
  pick="##faction250";frame();check(table.concat(lines," "):find("Effective 151",1,true)~=nil,"received details unavailable")
  check(#calls==0,"passive filters sent request")
  s.revision="changed again";frame();check(table.concat(lines," "):find("Effective 151",1,true)~=nil,"snapshot update cleared selected faction")
  clicks["Copy TSV"]=true;frame();check(clipboard and clipboard:find("server_faction_id",1,true)and clipboard:find("Faction 1\t",1,true)and clipboard:find("Faction 150\t",1,true),"export omitted hidden category/filter rows")
  clipboard=nil
  clicks["Export file"]=true;frame();check(calls[#calls]=="export","file export unavailable");calls={}
  s.pending=true;clicks["Refresh standings"]=true;frame();check(#calls==0,"pending duplicate")
  s.pending=false;s.fresh=false;clicks["Copy TSV"]=true;frame();check(table.concat(lines," "):find("Refresh to update",1,true)~=nil,"stale standings claimed current");check(not clipboard,"stale export allowed")
  clicks["Refresh standings"]=true;frame();check(#calls==1 and calls[1]=="refresh","explicit refresh absent")
  s.search_revision="command1";s.search="Faction 149";s.fresh=true;frame();check(options==1,"command search not applied")
  s.entries[#s.entries+1]={id=999,name="Behind Veil",standing="Indifferent",earned=1,effective=1,minimum=0,maximum=100,next_threshold=10,reaction=5,gauge=10,categories={4}}
  s.revision="hidden-record";s.search_revision="command2";s.search="Behind Veil";frame();check(options==1,"hidden-category faction unavailable to global search")
  clicks["Copy TSV"]=true;frame();check(clipboard:find("Behind Veil",1,true),"hidden-category faction omitted from export")
  s.revision="18446744073709551615";s.categories={};s.entries={};frame();check(options==0,"retired rows retained")
  -- XML art is optional, preserves its authored aspect and has a text fallback.
  local requested={}
  eq.UiAnimation=function(name)
    requested[#requested+1]=name
    if name=="Logo"or name=="LogoSelected"or name=="LogoHover"then return name,0.25,0.5,0.5,0.5625,128,32 end
  end
  s.revision="1";s.categories={{id=1,parent=0,order=1,name="Expansion",icon="Logo"}};s.entries={}
  harness.images={};frame()
  local image=harness.images[#harness.images]
  check(image and math.abs((image.x1-image.x0)/(image.y1-image.y0)-4)<0.0001,"category logo aspect distorted")
  pick="##category1";frame();frame();check(table.concat(requested,","):find("LogoSelected",1,true)~=nil,"selected art unused")
  pick="All factions";frame();ui.IsItemHovered=function()return true end;frame()
  check(table.concat(requested,","):find("LogoHover",1,true)~=nil,"hover art unused")
  eq.UiAnimation=function()return nil end;pick="Expansion##category1";frame();check(#calls==1,"missing art caused transport")
  check(begins==ends,"child clipping imbalance")
  harness.width=1920;harness.height=1080;frame();harness.width=1280;harness.height=720;frame()
  local requests=#calls
  harness.width=720;harness.height=700;frame();harness.width=440;harness.height=760;frame()
  check(#calls==requests,"responsive sort/footer layout emitted a request")
  harness.width=1280;harness.height=720;frame()
  harness.close=true;frame();check(calls[#calls]=="close"and not BASTION_UI.factions,"pack close did not close factions")
  local before=stateReads;for _=1,120 do frame()end
  check(stateReads==before,"closed factions kept reading its catalog")
  s.shown=true;s.revision="fresh-after-hidden";s.search_revision="fresh-hidden-search";s.search="Fresh faction"
  s.entries={{id=800,name="Fresh faction",standing="Indifferent",earned=1,effective=1,minimum=0,maximum=100,next_threshold=10,reaction=5,gauge=10,categories={1}}}
  local actions=#calls;frame()
  check(stateReads==before+1 and options==1 and #calls==actions,"native faction reopening did not read the fresh catalog")
  active=false;before=stateReads;frame();check(not BASTION_UI.factions and stateReads==before,"inactive factions fetched state or retained its local open bit")
  active=true;s.shown=false;frame();check(stateReads==before,"reactivated hidden factions fetched state")
  harness.split_layout(dofile("lib/citadel_received.lua"))
  harness.footer_layout(dofile("lib/citadel_received.lua"),{"Refresh standings","Export file","Copy TSV"})
  harness.finish()
end
print("Factions UI PASS "..checks.." checks")
