-- The real pack entry, pages and style run against bounded UI primitive mocks.
local H={}
function H.new(root,pack,check,plugin)
 local h={windows={},buttons={},selects={},boxes={},sliders={},inputs={},combos={},popups={},texts={},drawtexts={},bars={},rendered={},windowFlags={},childHeights={},tooltips={},readyBadges=0,hover=false,close=false,styles=0,vars=0,fonts=0,children=0,tables=0,frames=0,time=0,io={width=1280,height=720,uiScale=1}}
 local disabled={};local function noop()end
 local function text(v)h.texts[tostring(v)]=true end
 local function hit(label)h.rendered[label]=true;local value=h.buttons[label];h.buttons[label]=nil;return value==true and not disabled[#disabled]end
 local enum=setmetatable({},{__index=function()return 1 end})
 -- Exact WindowFlags exported by ebc7878a's lua_imgui.cpp. Unknown names stay nil.
 local windowFlags={None=0,NoTitleBar=1,NoResize=2,NoMove=4,NoScrollbar=8,NoScrollWithMouse=16,
  NoCollapse=32,AlwaysAutoResize=64,NoBackground=128,NoSavedSettings=256,MenuBar=1024,
  NoFocusOnAppearing=4096,NoDecoration=1|2|8|32,NoInputs=512|262144|524288,NoBringToFrontOnFocus=8192}
 BASTION_UI={};eq=setmetatable({Plugin=function(name)check(name=="bastion","Unexpected plugin");return plugin end},{__index=function(_,name)error("Unexpected action API "..name)end})
 local canonical=assert(loadfile(root.."/tests/host_ui_api_mock.lua"))()(eq,check)
 ui={Col=enum,StyleVar=enum,TableFlags=enum,TableColumnFlags=enum,Cond={Always=1,Appearing=2,FirstUseEver=3},WindowFlags=windowFlags,Mouse={Left=0},window=function(name,fn)h.windows[name]=fn end,
  GetIO=function()return h.io end,GetTime=function()return h.time end,
  SetNextWindowSize=function(w,hh)check(w<=h.io.width and hh<=h.io.height,"Window exceeds viewport")end,
  SetNextWindowPos=noop,SetNextWindowBgAlpha=noop,Begin=function(name,_,flags)
   h.windowFlags[name]=flags;local closed=name=="Raid Ready Check" and h.close
   if closed then h.close=false end;return true,not closed
  end,End=noop,
  BeginChild=function(id,w,hh)h.childHeights[id]=hh;h.children=h.children+1;check(w<=h.io.width and hh<=h.io.height,"Child exceeds viewport");return true end,
  EndChild=function()h.children=h.children-1;check(h.children>=0,"Unbalanced child")end,
  BeginTable=function()h.tables=h.tables+1;return true end,EndTable=function()h.tables=h.tables-1 end,TableSetupColumn=noop,TableNextRow=noop,TableNextColumn=noop,TableHeadersRow=noop,
  GetContentRegionAvail=function()return math.min(1000,h.io.width),h.contentHeight or 480 end,GetCursorScreenPos=function()return 20,40 end,SetCursorScreenPos=noop,GetWindowPos=function()return 20,30 end,GetWindowSize=function()return 900,500 end,SetCursorPos=noop,GetCursorPos=function()return 0,0 end,
  Text=text,TextWrapped=text,TextColored=function(_,_,_,_,v)text(v)end,Spacing=noop,Separator=noop,Dummy=noop,SameLine=noop,PushTextWrapPos=noop,PopTextWrapPos=noop,PushID=noop,PopID=noop,SetNextItemWidth=noop,
  PushStyleColor=function()h.styles=h.styles+1 end,PopStyleColor=function(n)h.styles=h.styles-(n or 1)end,PushStyleVar=function()h.vars=h.vars+1 end,PopStyleVar=function(n)h.vars=h.vars-(n or 1)end,
  PushFont=function()h.fonts=h.fonts+1 end,PopFont=function()h.fonts=h.fonts-1 end,
  BeginDisabled=function(v)disabled[#disabled+1]=v or disabled[#disabled]or false end,EndDisabled=function()table.remove(disabled)end,
  Button=hit,InvisibleButton=hit,Selectable=function(label)h.rendered[label]=true;local v=h.selects[label];h.selects[label]=nil;return v==true and not disabled[#disabled]end,
  Checkbox=function(label,value)h.rendered[label]=true;if h.boxes[label]~=nil then local v=h.boxes[label];h.boxes[label]=nil;return v,true end;return value,false end,
  SliderInt=function(label,value,min,max)h.rendered[label]=true;if h.sliders[label]~=nil then local v=h.sliders[label];h.sliders[label]=nil;check(v>=min and v<=max,"Test value exceeds field bounds");return v,true end;return value,false end,
  InputText=function(label,value)local v=h.inputs[label];return v~=nil and v or value,v~=nil end,BeginCombo=function(label)return h.combos[label]==true end,EndCombo=noop,CollapsingHeader=function()return false end,
  BeginPopupContextItem=function(label)return h.popups[label]==true end,EndPopup=noop,
  CalcTextSize=function(value)return #tostring(value)*7,h.textHeight or 14 end,IsItemHovered=function()return h.hover end,
  SetTooltip=function(value)h.tooltips[#h.tooltips+1]=value end,IsMouseDown=function()return false end,
  ProgressBar=function(value,width,height,label)check(value>=0 and value<=1,"Invalid resource fraction");h.bars[#h.bars+1]={value=value,width=width,height=height,label=label}end,
  DrawLine=noop,DrawRect=noop,DrawRectFilled=function(_,_,_,_,r,g,b,a)
   if r==.055 and g==.08 and b==.11 and a==.95 then h.readyBadges=h.readyBadges+1 end
  end,DrawRectGradient=noop,DrawCircle=noop,DrawCircleFilled=noop,DrawTriangleFilled=noop,
  DrawText=function(x,y,_,_,_,_,value,size)check(x==x and y==y and(not size or size>0),"Invalid preview geometry");h.drawtexts[#h.drawtexts+1]=value;text(value)end}
 local original=dofile
 dofile=function(path)
  if path=="lib/window.lua"then
   check(pack=="gamepad","Modern imported a core theme helper")
   return{T={S=2/3,W=1280,H=720,frame=function()h.frames=h.frames+1 end,font=function()h.fonts=h.fonts+1 end,unfont=function()h.fonts=h.fonts-1 end,
    fit=function(v,w)if #v*7<=w then return v end;return v:sub(1,math.max(0,math.floor(w/7)-3)).."..."end},
    begin=function(_,opts)check(opts.w*2/3<=h.io.width and opts.h*2/3<=h.io.height,"Controller frame exceeds viewport");return true end,
    area=function()return 20,70,h.io.width-20,h.io.height-60,h.io.width-40,h.io.height-130 end,finish=function()local v=h.close;h.close=false;return v end}
  end
  check(path:match("^lib/[%w_]+%.lua$")~=nil,"Unexpected helper path")
  return assert(loadfile(root.."/ui/"..pack.."/"..path))()
 end
 function h.load(path)assert(loadfile(root.."/ui/"..pack.."/"..path))()end
 function h.frame(name)
  h.texts={};h.drawtexts={};h.bars={};h.rendered={};h.windowFlags={};h.tooltips={};h.childHeights={};h.readyBadges=0
  assert(h.windows[name],"Window missing: "..name)()
  check(h.styles==0 and h.vars==0 and h.fonts==0 and h.children==0 and h.tables==0 and #disabled==0,"UI scope leaked")
 end
 function h.restore()canonical();dofile=original end
 return h
end
return H
