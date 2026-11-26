-- SPDX-License-Identifier: MIT
local guide = dofile("lib/expedition_journal_catalog.lua")
local mode = 1

return function(C, journal)
  C.header("Expedition Journal", "Castle Mistmoore")
  C.note("Guide only. Live status becomes available when the journal reconnects.", "muted")
  if C.button("Reconnect journal", 0, false, true) then journal("retry") end
  ui.SameLine()
  if C.button("Open Bastion Library", 0, false, true) then journal("open_library") end

  if ui.BeginCombo("Difficulty", guide[mode].name) then
    for index, entry in ipairs(guide) do
      if ui.Selectable(entry.name, index == mode) then mode = index end
    end
    ui.EndCombo()
  end
  local entry = guide[mode]
  C.section(entry.title)
  C.note(entry.entry)
  C.note(entry.rules)
  C.note(entry.unlock)
  for _, encounter in ipairs(entry.encounters) do
    C.section(encounter[1])
    C.note(encounter[2])
  end
end
