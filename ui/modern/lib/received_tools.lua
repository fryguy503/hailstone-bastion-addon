-- A bounded desktop frame. Long lists and confirmations remain in scrollable
-- content at small viewports instead of extending below the screen.
local M={scale=1,width=1,height=1}
local positions={}
local child=false
function M.begin(title,width,height)
  local io=ui.GetIO()
  M.scale=math.max(0.5,math.min(3,io.uiScale or 1))
  local maximumW,maximumH=math.max(1,(io.width or 1920)-24),math.max(1,(io.height or 1080)-24)
  local w,h=math.min(width*M.scale,maximumW),math.min(height*M.scale,maximumH)
  ui.SetNextWindowSize(w,h,ui.Cond.Always)
  ui.SetNextWindowPos(math.max(0,((io.width or 1920)-w)/2),math.max(0,((io.height or 1080)-h)/2),ui.Cond.FirstUseEver)
  local previous=positions[title]
  if previous then
    local x=math.max(0,math.min(previous.x,(io.width or 1920)-w))
    local y=math.max(0,math.min(previous.y,(io.height or 1080)-h))
    if x~=previous.x or y~=previous.y then ui.SetNextWindowPos(x,y,ui.Cond.Always)end
  end
  local visible,open=ui.Begin(title,true)
  if ui.GetWindowPos then
    local x,y=ui.GetWindowPos();positions[title]={x=x,y=y}
  end
  child=false
  if visible then
    M.width,M.height=ui.GetContentRegionAvail()
    M.width,M.height=math.max(1,M.width),math.max(1,M.height)
    child=true
    visible=ui.BeginChild("##received-tool-body:"..title,0,0,false)
  end
  return visible,open
end
function M.finish()
  if child then ui.EndChild()end
  ui.End()
  return false
end
function M.list_height(design)
  return math.max(48,math.min(design*M.scale,M.height*0.43))
end
return M
