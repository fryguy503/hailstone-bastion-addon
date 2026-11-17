-- Actual gamepad frame: focus, pad hints, B-close and theme fonts are supplied
-- by the core pack, while every tool keeps its own bounded data/action model.
local W=dofile("lib/window.lua")
local T=dofile("lib/theme.lua")
local M={scale=1,width=1,height=1}
local child=false
function M.begin(title,width,height)
  T.frame()
  M.scale=T.S
  local w=math.min(math.max(1100,width*1.55),math.max(1,(T.W-40)/T.S))
  local h=math.min(math.max(880,height*1.45),math.max(1,(T.H-40)/T.S))
  local visible=W.begin(title,{w=w,h=h,title=title,subtitle="Citadel | Your Bastion character",
    hints={{"a","Choose"},{"b","Close"}}})
  child=false
  if visible then
    local x,y,_,_,cw,ch=W.area()
    M.width,M.height=math.max(1,cw),math.max(1,ch)
    ui.SetCursorScreenPos(x,y)
    local availableW,availableH=ui.GetContentRegionAvail()
    M.width,M.height=math.min(M.width,math.max(1,availableW)),math.min(M.height,math.max(1,availableH))
    child=true
    visible=ui.BeginChild("##received-tool-body:"..title,M.width,M.height,false)
  end
  return visible,true
end
function M.finish()
  if child then ui.EndChild()end
  return W.finish()
end
function M.list_height(design)
  return math.max(T.px(72),math.min(T.px(design),M.height*0.43))
end
return M
