-- SPDX-License-Identifier: MIT
-- Stock Raid controls use owner-scoped Lua extensions; their requests stay in the addon backend.
local Host=dofile("lib/bastion_host.lua")
local M={}
local window,section="RaidWindow","member_actions"
local definitions={
 {id="bastion.manager.list",label="List managers",selection=false,review=false,priority=0},
 {id="bastion.manager.grant",label="Grant manager",selection=true,review=true,priority=10},
 {id="bastion.manager.revoke",label="Revoke manager",selection=true,review=true,priority=20},
}
local handles,remove,generation={},nil,0
local function extensions()
 local api=type(hs)=="table"and hs.UI or nil
 local service=type(api)=="table"and api.extensions or nil
 if type(service)=="table"and type(service.RegisterAction)=="function"and type(service.Remove)=="function"then
  local live=service.IsRegistered
  return service.RegisterAction,service.Remove,type(live)=="function"and live or nil
 end
end
local function clear()
 generation=generation+1
 local old,release=handles,remove;handles={};remove=nil
 -- The host also retires owner handles on unload; already-retired handles need no further work.
 if release then for _,handle in ipairs(old)do pcall(release,handle)end end
end
local function unavailable(message)return{ok=false,message=message or "Bastion raid manager requests are unavailable."}end
local function callback(definition,version)
 return function(target,id,phase,context,reviewToken)
  if version~=generation or #handles~=#definitions or target~=window or id~=definition.id then return unavailable()end
  if definition.review then
   if phase~="review"and phase~="confirm"and phase~="cancel"then return unavailable("Review this delegation before confirming it.")end
  elseif phase~="invoke"then return unavailable()end
  local selection=type(context)=="table"and context.selection or nil
  local name=type(selection)=="table"and selection.name or ""
  if type(name)~="string"then return unavailable("Choose a current raid character.")end
  local plugin=Host.Plugin("bastion")
  if not plugin or type(plugin.raid_manager_action)~="function"then return unavailable()end
  local response=plugin.raid_manager_action(target,id,phase,name,reviewToken or "")
  if type(response)~="table"then return unavailable()end
  local result={ok=response.ok==1,message=type(response.message)=="string"and response.message or "Raid manager request unavailable."}
  if type(response.review_token)=="string"and response.review_token~=""then result.reviewToken=response.review_token end
  if result.ok and phase=="review"and not result.reviewToken then return unavailable("A new delegation review could not be created.")end
  return result
 end
end
function M.sync(plugin,state)
 local add,release,isRegistered=extensions()
 local available=plugin and type(plugin.raid_manager_action)=="function"and state and state.available==1 and state.in_zone==1 and state.in_raid==1 and state.manager_supported==1
 if not available or not add then if #handles>0 then clear()end;return end
 -- Source-bound API methods may be freshly wrapped on every lookup. Opaque handles live until
 -- removal or owner/source-generation teardown, so method identity must not retire a pending review.
 if #handles==#definitions then
  local live=true
  if isRegistered then
   for _,handle in ipairs(handles)do
    local okay,registered=pcall(isRegistered,handle)
    if not okay or registered~=true then live=false;break end
   end
  end
  if live then return end
 end
 clear();remove=release
 local version=generation
 for _,definition in ipairs(definitions)do
  local okay,handle=pcall(add,window,section,definition.id,definition.label,
   {requiresSelection=definition.selection,requiresReview=definition.review,requiredCaps={"chat"},priority=definition.priority},callback(definition,version))
  if not okay or not handle then clear();return end
  handles[#handles+1]=handle
 end
end
function M.remove()clear()end
return M
