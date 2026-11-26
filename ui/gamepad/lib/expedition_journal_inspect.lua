-- SPDX-License-Identifier: MIT
local Host = dofile("lib/bastion_host.lua")
local M = {}

function M.item(style, name, id, key, enabled)
  if not enabled or type(id) ~= "number" or id < 1 or id > 0xffffffff or id % 1 ~= 0 then return false end
  local plugin = Host.Plugin("bastion")
  local journal = plugin and plugin.expedition_journal
  if type(journal) ~= "function" then return false end
  if style.button(name .. "##journal-item-" .. key) then journal("inspect_item", string.format("%d", id)) end
  return true
end

local function spellMethod(name, previous)
  local api = type(hs) == "table" and hs.UI or nil
  local spells = type(api) == "table" and api.spells or nil
  local call = type(spells) == "table" and spells[name] or nil
  if type(call) ~= "function" then call = type(hs) == "table" and hs[previous] or nil end
  if type(call) ~= "function" then call = type(eq) == "table" and eq[previous] or nil end
  if type(call) == "function" then return call end
end

function M.spell(style, id, key, scale, titles)
  local show = spellMethod("Show", "ShowSpell")
  local describe = spellMethod("Details", "SpellDetails")
  if not show or not describe then return end
  local title = titles[id]
  if title == nil then
    local ok, displayTitle = pcall(describe, id)
    -- The native title prefixes the actual spell name with a localized label.
    local name = ok and type(displayTitle) == "string" and (displayTitle:match("^.-  (.*)$") or displayTitle)
    title = name and name ~= "" and name or false
    titles[id] = title
  end
  if title and style.guideButton("Spell · " .. title, key .. ":" .. id, scale) then show(id) end
end

return M
