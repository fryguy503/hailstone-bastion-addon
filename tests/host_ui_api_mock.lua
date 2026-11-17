-- Use the selected client's generated descriptor mapping for its actual modular pack helpers.
-- Legacy helpers can still read eq; addon plugin calls must always use the canonical facade.
return function(legacy, check)
  local plugin = assert(legacy.Plugin)
  local reads = 0
  legacy.Plugin = function() error("Addon used legacy Plugin instead of the canonical UI API") end
  local flat = {}
  for key,value in pairs(legacy)do flat[key]=value end
  flat.Plugin = function(...)
      reads = reads + 1
      return plugin(...)
  end
  flat.ItemIcon = function(...)
    local call = rawget(legacy, "ItemIcon") -- mutable fixture data, not a production proxy
    if call then return call(...) end
  end
  flat.UiAnimation = function(...)
    local call = rawget(legacy, "UiAnimation")
    if call then return call(...) end
  end
  local client=CLIENT_ROOT or os.getenv("HAILSTONE_CLIENT_UI_ROOT")
  local generated=client and loadfile(client.."/tests/lua_ui_api_mock.lua")
  local mode="legacy core helpers, canonical-preferred addon access"
  if generated then
    assert(generated())(flat)
    mode="actual modular core helpers, descriptor-generated facade"
  else
    -- Isolated/older-host tests need only these three addon methods. Never use this partial
    -- facade for a new core: its native helpers require the complete generated contract.
    flat.UI={version={major=1,minor=2},plugins={Get=flat.Plugin},
      assets={ItemIcon=flat.ItemIcon,Animation=flat.UiAnimation}}
  end
  local views=setmetatable({},{__mode="k"})
  local function readonly(value)
    if type(value)~="table"then return value end
    if views[value]then return views[value]end
    local view=setmetatable({},{__index=function(_,key)return readonly(value[key])end,
      __newindex=function()error("Fixture host proxy was mutated")end,
      __pairs=function()error("Fixture host proxy was enumerated")end,__metatable=false})
    views[value]=view;return view
  end
  hs=readonly(flat)
  return function()check(reads>0,"Canonical plugins.Get was not exercised")end,mode
end
