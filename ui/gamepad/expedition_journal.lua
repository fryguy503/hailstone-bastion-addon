-- SPDX-License-Identifier: MIT
local Host = dofile("lib/bastion_host.lua")
local R = dofile("lib/citadel_received.lua")
local View = dofile("lib/expedition_journal_view.lua")
local view = View.new()
local wasShown = false

ui.window("Expedition Journal", function()
  local plugin = Host.Plugin("bastion")
  local journal = plugin and plugin.expedition_journal
  local state = journal and journal("window")
  if not state or not state.shown then
    if wasShown then View.reset(view) end
    wasShown = false
    return
  end
  wasShown = true

  local visible, open = R.begin("Expedition Journal", 1050, 825)
  if visible and open then View.draw(view, state, journal, R.s or 1) end
  local closed = R.finish()
  if not open or closed then journal("close") end
end)
