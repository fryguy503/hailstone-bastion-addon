-- SPDX-License-Identifier: MIT
-- Local display preferences; the native provider retains tracking and map permissions.
local Host=dofile("lib/bastion_host.lua")
local C=dofile("lib/citadel_style.lua")
local M={}
local window,section="MapViewWnd","controls"
local handle,remove,generation=nil,nil,0
local members={{"map_group","Group members"},{"map_raid","Raid names"}}
local tracked={{"map_mobs","NPCs"},{"map_merchants","Merchants"},
  {"map_players","Other players"},{"map_corpses","Corpses"}}
local function rows(plugin,state,definitions,enabled,apply)
  for _,definition in ipairs(definitions)do
    ui.BeginDisabled(not enabled)
    local value,changed=ui.Checkbox(definition[2],(state.settings or{})[definition[1]]==1)
    ui.EndDisabled()
    if changed and enabled then
      if apply then apply(definition[1],value and 1 or 0)
      else plugin.citadel("set",definition[1],value and 1 or 0)end
    end
  end
end
function M.draw_settings(plugin,state,style,apply)
  rows(plugin,state,members,true,apply)
  rows(plugin,state,tracked,state.can_track==true,apply)
  style.note(state.can_track and "Tracking range applies to NPCs and corpses."or"Trained Tracking required.","muted")
  style.tooltip("Choose Bastion markers. Stock NPC overlays are hidden. Compatible clients can Find NPCs advertised by the Bastion zone server. Older clients keep NPC Find disabled. Your own marker stays visible.")
end
local function services()
  local api=type(hs)=="table"and hs.UI or nil
  local extension=type(api)=="table"and api.extensions or nil
  local map=type(api)=="table"and api.map or nil
  if type(extension)=="table"and type(extension.RegisterSection)=="function"and
      type(extension.Remove)=="function"and type(extension.IsRegistered)=="function"and
      type(map)=="table"and type(map.Policy)=="function"then
    return extension.RegisterSection,extension.Remove,extension.IsRegistered,map.Policy
  end
end
local function clear()
  generation=generation+1
  local old,release=handle,remove;handle,remove=nil,nil
  if old and release then pcall(release,old)end
end
local function current(context)
  local _,_,isRegistered,read=services()
  local expected=type(context)=="table"and context.policy or nil
  if not handle or not read or type(expected)~="table"or type(expected.context)~="string"or expected.context==""or
      type(expected.revision)~="string"or expected.revision==""then return end
  local admitted,registered=pcall(isRegistered,handle)
  if not admitted or registered~=true then return end
  local okay,policy=pcall(read)
  if not okay or type(policy)~="table"or policy.context~=expected.context or policy.revision~=expected.revision then return end
  if expected.source~=nil or policy.source~=nil then
    if type(expected.source)~="string"or expected.source==""or policy.source~=expected.source then return end
  end
  local plugin=Host.Plugin("bastion")
  local state=plugin and type(plugin.citadel)=="function"and plugin.citadel("state")
  if type(state)=="table"and state.active==true then return plugin,state end
end
local function callback(version)
  return function(target,slot,placement,context)
    if version~=generation or not handle or target~=window or slot~=section or placement~="after"then return end
    local plugin,state=current(context)
    if not plugin then return end
    C.push()
    C.section("Bastion filters")
    M.draw_settings(plugin,state,C,function(key,value)
      if version~=generation or not handle then return end
      local live,fresh=current(context)
      if live and (key=="map_group"or key=="map_raid"or fresh.can_track==true)then live.citadel("set",key,value)end
    end)
    C.pop()
  end
end
function M.sync(plugin,state)
  local add,release,isRegistered=services()
  if not plugin or type(plugin.citadel)~="function"or type(state)~="table"or not state.active or not add then
    if handle then clear()end
    return
  end
  if handle then
    local okay,live=pcall(isRegistered,handle)
    if okay and live==true then return end
  end
  clear();remove=release
  local okay,registered=pcall(add,window,section,"after","bastion.map.filters",callback(generation),{priority=0})
  if okay and registered then handle=registered else clear()end
end
function M.remove()clear()end
return M
