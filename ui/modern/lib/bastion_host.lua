-- SPDX-License-Identifier: MIT
-- Keep host lookups live: the UI facade may be a guarded proxy and plugins can unload between frames.
local M = {}
local function method(module, name, previous)
  local api = type(hs) == "table" and hs.UI or nil
  local service = type(api) == "table" and api[module] or nil
  local current = type(service) == "table" and service[name] or nil
  if type(current) == "function" then return current end
  local legacy = type(eq) == "table" and eq[previous] or nil
  if type(legacy) == "function" then return legacy end
end
function M.Plugin(name)
  local call = method("plugins", "Get", "Plugin")
  if call then return call(name) end
end
function M.ItemIcon(icon)
  local call = method("assets", "ItemIcon", "ItemIcon")
  if call then return call(icon) end
end
function M.UiAnimation(name)
  local call = method("assets", "Animation", "UiAnimation")
  if call then return call(name) end
end
return M
