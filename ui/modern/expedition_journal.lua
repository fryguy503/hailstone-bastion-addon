-- SPDX-License-Identifier: MIT
local Host = dofile("lib/bastion_host.lua")
local R = dofile("lib/citadel_received.lua")
local drawGuide = dofile("lib/expedition_journal_guide.lua")

ui.window("Expedition Journal", function()
  local plugin = Host.Plugin("bastion")
  local journal = plugin and plugin.expedition_journal
  local state = journal and journal("window")
  if not state or not state.shown then return end

  local visible, open = R.begin("Expedition Journal", 1050, 825)
  if visible and open then
    if state.tab ~= 0 and type(hs.WebView) == "function" then
      local width, height = ui.GetContentRegionAvail()
      hs.WebView(state.tab, math.max(1, width), math.max(1, height))
    else
      drawGuide(R.C, journal)
    end
  end
  local closed = R.finish()
  if not open or closed then journal("close") end
end)
