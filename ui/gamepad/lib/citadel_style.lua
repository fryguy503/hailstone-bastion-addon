-- Shared presentation for Bastion's authored Citadel tools. No saved settings or game actions.
local C = {}
C.colors = {
  gold={0.96,0.75,0.36,1}, text={0.90,0.94,0.98,1}, muted={0.59,0.68,0.77,1},
  good={0.35,0.83,0.65,1}, warn={0.98,0.67,0.32,1}, danger={0.97,0.38,0.36,1},
  panel={0.055,0.085,0.12,1}, border={0.22,0.31,0.40,1}, blue={0.38,0.72,0.96,1},
}
local colors=C.colors
local function rgba(c) return c[1],c[2],c[3],c[4] or 1 end
local function tone(c) return type(c)=="table" and c or colors[c or "text"] or colors.text end
local function text(c,value) ui.TextColored(c[1],c[2],c[3],c[4] or 1,value) end
function C.scale() return math.max(0.65,math.min(2,(ui.GetIO().uiScale or 1))) end
function C.push()
  local s=C.scale()
  local entries={
    {"Text",colors.text},{"TextDisabled",colors.muted},{"WindowBg",{0.035,0.055,0.08,0.98}},
    {"ChildBg",colors.panel},{"Border",colors.border},{"FrameBg",{0.085,0.13,0.18,1}},
    {"FrameBgHovered",{0.14,0.23,0.31,1}},{"FrameBgActive",{0.19,0.31,0.41,1}},
    {"Button",{0.11,0.19,0.26,1}},{"ButtonHovered",{0.20,0.34,0.44,1}},
    {"ButtonActive",{0.26,0.43,0.55,1}},{"Header",{0.14,0.25,0.34,1}},
    {"HeaderHovered",{0.20,0.35,0.44,1}},{"HeaderActive",{0.25,0.41,0.52,1}},
    {"CheckMark",colors.gold},{"SliderGrab",colors.gold},{"SliderGrabActive",{1,0.86,0.57,1}},
    {"TitleBg",{0.055,0.095,0.14,1}},{"TitleBgActive",{0.10,0.18,0.25,1}},
  }
  local count=0
  for _,entry in ipairs(entries) do
    if ui.Col[entry[1]] then ui.PushStyleColor(ui.Col[entry[1]],rgba(entry[2]));count=count+1 end
  end
  local vars=0
  for _,entry in ipairs({{"WindowPadding",14*s,12*s},{"FramePadding",9*s,5*s},{"ItemSpacing",10*s,7*s},{"FrameRounding",3*s}})do
    if ui.StyleVar[entry[1]] then
      if entry[3] then ui.PushStyleVar(ui.StyleVar[entry[1]],entry[2],entry[3]) else ui.PushStyleVar(ui.StyleVar[entry[1]],entry[2]) end
      vars=vars+1
    end
  end
  C.stack=C.stack or {};C.stack[#C.stack+1]={count,vars}
end
function C.pop()
  local entry=table.remove(C.stack or {})
  if entry then ui.PopStyleVar(entry[2]);ui.PopStyleColor(entry[1]) end
end
function C.begin(title,open,w,h)
  C.push()
  local s=C.scale();local io=ui.GetIO()
  local vw=io.width or 1920;local vh=io.height or 1080
  local width=math.min(w*s,math.max(320,vw-40*s));local height=math.min(h*s,math.max(280,vh-60*s))
  ui.SetNextWindowSize(width,height,ui.Cond.FirstUseEver)
  ui.SetNextWindowPos((vw-width)/2,(vh-height)/2,ui.Cond.FirstUseEver)
  C.rects=C.rects or {};local old=C.rects[title]
  if old then
    local cw,ch=math.min(old.w,vw-20*s),math.min(old.h,vh-20*s)
    if cw~=old.w or ch~=old.h then ui.SetNextWindowSize(cw,ch,ui.Cond.Always) end
    local x=math.max(10*s,math.min(old.x,vw-cw-10*s));local y=math.max(10*s,math.min(old.y,vh-ch-10*s))
    if x~=old.x or y~=old.y then ui.SetNextWindowPos(x,y,ui.Cond.Always) end
  end
  local visible,stillOpen=ui.Begin(title,open)
  if ui.GetWindowSize then
    local x,y=ui.GetWindowPos();local cw,ch=ui.GetWindowSize();C.rects[title]={x=x,y=y,w=cw,h=ch}
  end
  return visible,stillOpen
end
function C.finish() ui.End();C.pop() end
function C.note(text,color)
  if not text or text=="" then return end
  ui.PushTextWrapPos(0);local c=tone(color);ui.TextColored(c[1],c[2],c[3],c[4] or 1,text);ui.PopTextWrapPos()
end
function C.header(title,subtitle,tag)
  local s=C.scale()
  ui.PushFont(6);text(colors.blue,title);ui.PopFont()
  if tag and tag~="" then ui.SameLine();text(colors.gold,tag) end
  if subtitle and subtitle~="" then C.note(subtitle,"muted") end
  local x,y=ui.GetCursorScreenPos();local w=ui.GetContentRegionAvail()
  local b=colors.border;ui.DrawLine(x,y+2*s,x+w,y+2*s,b[1],b[2],b[3],b[4],1)
  ui.Dummy(0,8*s)
end
function C.section(title,hint)
  text(colors.gold,title)
  if hint and hint~="" then C.note(hint,"muted") end
  ui.Separator()
end
function C.field(label,value,color)
  text(colors.muted,label);ui.SameLine();text(tone(color),tostring(value or "Unavailable"))
end
function C.tooltip(text)
  if text and ui.IsItemHovered() then ui.SetTooltip(text) end
end
function C.button(label,width,primary,...)
  local enabled,height=...
  -- An omitted enable argument is the ordinary active button. An explicitly
  -- supplied nil is an unavailable selection, as in row and row.can_choose.
  enabled=select("#",...)==0 or enabled==true
  local count=0
  if primary then
    local c=primary=="danger" and colors.danger or colors.gold
    ui.PushStyleColor(ui.Col.Button,c[1]*0.29,c[2]*0.29,c[3]*0.29,1)
    ui.PushStyleColor(ui.Col.ButtonHovered,c[1]*0.44,c[2]*0.44,c[3]*0.44,1)
    ui.PushStyleColor(ui.Col.ButtonActive,c[1]*0.58,c[2]*0.58,c[3]*0.58,1)
    count=3
  end
  ui.BeginDisabled(not enabled)
  local clicked=ui.Button(label,width or 0,height or 0)
  ui.EndDisabled()
  if count>0 then ui.PopStyleColor(count) end
  return clicked and enabled
end
function C.tabs(id,labels,current)
  local s=C.scale();local w=ui.GetContentRegionAvail();local bw=math.max(44*s,(w-10*s*(#labels-1))/#labels)
  for i,label in ipairs(labels)do
    if i>1 then ui.SameLine() end
    if C.button(label.."##"..id,bw,i==current,true,30*s) then current=i end
  end
  ui.Dummy(0,3*s)
  return current
end
function C.card(id,w,h,draw)
  local visible=ui.BeginChild(id,w or 0,h or 0,true)
  if visible then draw() end
  ui.EndChild()
end
function C.split(id,leftWidth,height,left,right)
  local s=C.scale();local available=ui.GetContentRegionAvail()
  local width=math.min(leftWidth,math.max(140*s,available*0.48))
  C.card(id.."-left",width,height,left);ui.SameLine()
  C.card(id.."-right",0,height,right)
end
function C.gauge(label,fraction,display,color,height)
  local s=C.scale();local c=tone(color or "good")
  if label and label~="" then text(colors.muted,label) end
  local x,y=ui.GetCursorScreenPos();local w=ui.GetContentRegionAvail();local h=height or 24*s
  ui.DrawRectFilled(x,y,x+w,y+h,0.025,0.04,0.06,1,3*s)
  if fraction then
    local fill=math.max(0,math.min(1,fraction))
    if fill>0 then ui.DrawRectFilled(x+1,y+1,x+math.max(1,(w-2)*fill),y+h-1,c[1],c[2],c[3],0.75,2*s) end
  end
  local b=colors.border;ui.DrawRect(x,y,x+w,y+h,b[1],b[2],b[3],b[4],3*s,1)
  local text=display or (fraction and string.format("%.0f%%",fraction*100) or "Unavailable")
  local tw,th=ui.CalcTextSize(text)
  local t=colors.text;ui.DrawText(x+math.max(5*s,(w-tw)/2),y+(h-th)/2,t[1],t[2],t[3],t[4],text)
  ui.Dummy(w,h)
end
return C
