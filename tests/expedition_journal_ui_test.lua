-- SPDX-License-Identifier: MIT
local root = ADDON_ROOT or (arg and arg[1]) or "."
local setup = assert(loadfile(root .. "/tests/received_tools_ui_harness.lua"))()
local checks = 0

local function check(ok, why)
  checks = checks + 1
  if not ok then error(why, 2) end
end

local function fixtures()
  local relic = {
    relicId = 170009, name = "Jelvan", quantity = 1, credit = "credited",
    achievementId = 1800100201, componentId = 1800200211, componentType = 1,
  }
  local detail = {}
  for key, value in pairs(relic) do detail[key] = value end
  detail.rewards = {
    {itemId = 47308, name = "Bazu Claw Hammer", availability = "available"},
    {itemId = 47300, name = "Mask of Lament", availability = "lore_conflict"},
  }
  return {
    ["normal:0"] = {
      serverNow = 1000, unlock = {state = "unlocked"},
      run = {state = "rejoin", expiresAt = 8200, sealed = true},
      replay = {state = "locked", expiresAt = 260200},
      roster = {state = "ready", kind = "run", members = {
        {name = "Journal Member", state = "eligible", isLeader = true},
      }},
    },
    ["hard:0"] = {serverNow = 1000, unlock = {state = "locked"}, run = {state = "none"}, replay = {state = "clear"}},
    ["vigil:0"] = {serverNow = 1000, unlock = {state = "not_required"}, run = {state = "none"}, replay = {state = "clear"}},
    ["copium:0"] = {serverNow = 1000, relics = {relic}},
    ["copium:170009"] = {serverNow = 1000, relic = detail},
    ["sleepers:0"] = {serverNow = 1000, publicPhase = "2.0", nextPhase = "1.0", rotation = "alternating",
      nextRotationAt = 8200, savedRun = {phase = "2.0", expiresAt = 1060}},
    ["tofs_group:0"] = {serverNow = 1000, unlock = {state = "not_required"},
      run = {state = "rejoin", expiresAt = 8200}, replay = {state = "locked", expiresAt = 22600},
      roster = {kind = "run", state = "ready", members = {{name = "Assigned Group Member", state = "eligible"}}}},
    ["tofs_raid:0"] = {serverNow = 1000, unlock = {state = "unlocked", payment = "paid",
      items = {{name = "Splinters of the Unwritten Hour", quantity = 2, required = 3}}},
      run = {state = "resume", expiresAt = 8200}, replay = {state = "clear"}},
    ["broken_accord:0"] = {serverNow = 1000, unlock = {state = "locked", reason = "personal_velious_required"},
      run = {state = "none"}, replay = {state = "clear"}, roster = {state = "unavailable", reason = "leader_only"}},
    ["firefell:0"] = {serverNow = 1000, unlock = {state = "unlocked"},
      run = {state = "none"}, replay = {state = "locked", expiresAt = 2200},
      completion = {state = "locked", expiresAt = 44200}, chapters = {
        {name = "Velious", state = "unlocked", roster = {kind = "request", state = "ready",
          members = {{name = "Velious Tester", state = "eligible"}}}},
        {name = "Luclin", state = "locked", reason = "chapter_locked", roster = {kind = "request", state = "blocked",
          members = {{name = "Luclin Tester", state = "blocked", reason = "chapter_locked"}}}},
      }},
  }
end

for _, pack in ipairs({"modern", "gamepad"}) do
  local draw, clicks, texts = nil, {}, {}
  local selected, refreshed = {}, {}
  local snapshots, closed = 0, 0
  local search, combo, scroll, detailLoads = nil, nil, 0, 0
  local state = {shown = false, connected = true, character = "Journal Character", revision = "1", generation = "1"}
  local data = fixtures()
  local plugin = {
    expedition_journal = function(op, mode, id)
      if op == "window" then return state end
      if op == "close" then state.shown = false; closed = closed + 1; return end
      local key = mode .. ":" .. id
      if op == "select" then selected[#selected + 1] = key
      elseif op == "refresh" then refreshed[#refreshed + 1] = key
      elseif op == "snapshot" then snapshots = snapshots + 1; return {data = data[key]}
      else error("Unexpected Expedition Journal operation " .. tostring(op)) end
    end,
  }
  eq = {Plugin = function() return plugin end}
  local function buttonClicked(label)
    local id = label:match("##.*$")
    local click = clicks[label] or clicks[id]
    clicks[label] = nil
    if id then clicks[id] = nil end
    return click
  end
  ui = {
    window = function(_, fn) draw = fn end,
    Begin = function() return true, true end, End = function() end,
    BeginChild = function() return true end, EndChild = function() end,
    TextWrapped = function(text) texts[#texts + 1] = text end,
    Button = buttonClicked, Selectable = buttonClicked,
    SameLine = function() end,
    SetScrollY = function(value) scroll = value end,
    GetScrollY = function() return scroll end,
    InputTextWithHint = function(_, _, value) return search or value end,
    BeginCombo = function(id) return id == combo end,
    EndCombo = function() end,
  }
  local harness = setup(root, pack, ui, eq, check)
  local guideTable, guideColumns, guideWeights = false, 0, {}
  local beginTable, endTable, setupColumn = ui.BeginTable, ui.EndTable, ui.TableSetupColumn
  function ui.BeginTable(id, columns, ...)
    guideTable = id == "##journal-guardian-table"
    if guideTable then guideColumns, guideWeights = columns, {} end
    return beginTable(id, columns, ...)
  end
  function ui.TableSetupColumn(label, flags, weight)
    if guideTable then guideWeights[#guideWeights + 1] = weight end
    return setupColumn(label, flags, weight)
  end
  function ui.EndTable(...)
    guideTable = false
    return endTable(...)
  end
  local packDofile = dofile
  dofile = function(path)
    if path:match("^lib/encounter_library/detail_") then detailLoads = detailLoads + 1 end
    return packDofile(path)
  end
  dofile(root .. "/ui/" .. pack .. "/expedition_journal.lua")
  local function frame()
    texts = {}
    harness.advance()
    draw()
    harness.balanced()
  end
  local function shows(value)
    return table.concat(texts, "\n"):find(value, 1, true) ~= nil
  end
  local function click(label)
    clicks[label] = true
    frame()
  end

  for _ = 1, 30 do frame() end
  check(snapshots == 0 and #selected == 0, "hidden journal read or requested server snapshots")

  state.shown = true
  frame()
  check(shows("Last Eclipse") and shows("Rejoin your saved run") and shows("2h left") and shows("3d left"),
    "native overview lost the saved run or separate replay clock")
  check(shows("Archivist of the Eclipse") and shows("Huntmaster Draeven"), "native overview omitted the packaged guide")
  check(guideColumns == 2 and #guideWeights == 2 and guideWeights[1] == 1 and guideWeights[2] == 1,
    "wide guardian guide let content set unequal column widths")
  local initialSnapshots = snapshots
  for _ = 1, 20 do frame() end
  check(snapshots == initialSnapshots and #selected == 1, "stable native frames rebuilt the snapshot or sent requests")
  check(detailLoads == 0, "overview eagerly loaded encounter details")

  scroll = 124
  click("##journal-guide:expedition:gravekeeper-vhal")
  frame()
  check(shows("Ring of Blood") and shows("Break the Grave Seal") and shows("Tanks") and shows("Stay in the battle"),
    "Mistmoore guardian did not open its published ability, phase, role, and encounter notes")
  check(detailLoads == 1 and snapshots == initialSnapshots and #selected == 1,
    "native guardian details required a live query or repeatedly loaded their data")
  click("Back to expedition##journal-guide-back")
  frame()
  check(shows("Rejoin your saved run") and scroll == 124, "guardian back did not restore the expedition and scroll")

  click("Roster##journal-tab")
  check(shows("Journal Member · Leader") and shows("Can rejoin"), "saved run roster omitted the named leader or eligibility")
  harness.frame = harness.frame + 60 * 31
  frame()
  check(shows("Refresh to check the current roster.") and not shows("Journal Member"), "stale roster kept disclosing member rows")
  click("Refresh##journal-status")
  check(refreshed[#refreshed] == "normal:0", "native refresh did not request the current roster mode")

  click("Bloodbound##journal-mode")
  click("Overview##journal-tab")
  check(selected[#selected] == "hard:0" and shows("Writ required") and shows("Bloodbound requests"),
    "Bloodbound selection used the wrong live mode or unlock")
  click("Midnight Vigil##journal-route-2")
  check(selected[#selected] == "vigil:0" and shows("Admission relics") and shows("3–6 characters"),
    "Midnight Vigil omitted its mode and prerequisites")

  click("Copium tracker##journal-route-3")
  check(shows("Exact credit") and shows("1800200211") and shows("Ready to visit Copium")
    and shows("Bazu Claw Hammer") and shows("Lore conflict"), "native Copium detail omitted credit or vendor choices")
  check(selected[#selected] == "copium:170009", "Copium picked the wrong relic detail")

  local liveSnapshots, liveSelections = snapshots, #selected
  click("##journal-route-4")
  frame()
  check(shows("Encounter Library") and shows("Acrylia Caverns") and shows("Anguish, the Fallen Palace"),
    "native library omitted published encounters from multiple zones")
  click("Next##journal-library-next")
  check(shows("13–24 of") and not shows("Acrylia Caverns"), "library could not advance through more encounters")
  search = "sERu lord"
  frame()
  check(shows("Lord Inquisitor Seru") and shows("1–1 of 1 encounters"), "search failed to match terms across the encounter index")
  combo = "##journal-library-kind"
  click("Event##journal-library-kind:event")
  check(shows("No encounters match these filters."), "encounter-type filter did not narrow the search results")
  click("Raid##journal-library-kind:raid")
  combo = "##journal-library-zone"
  click("Acrylia Caverns##journal-library-zone:Acrylia Caverns")
  check(shows("No encounters match these filters."), "zone filter did not narrow the search results")
  click("Sanctus Seru##journal-library-zone:Sanctus Seru")
  combo = nil
  check(shows("Lord Inquisitor Seru") and shows("1–1 of 1 encounters"), "zone and raid filters lost the matching encounter")
  click("##journal-guide:library:sseru-lord-inquisitor-seru")
  frame()
  check(shows("Encounter plan") and shows("Lord Inquisitor Seru") and shows("Role tips") and detailLoads == 2,
    "native library did not load the selected encounter detail lazily")
  click("Back to encounters##journal-guide-back")
  search = nil
  frame()
  check(shows("1–1 of 1 encounters") and snapshots == liveSnapshots and #selected == liveSelections,
    "returning to the library lost filters or requested live expedition state")
  click("##journal-route-3")

  state.generation, state.revision, state.connected = "2", "2", false
  frame()
  check(shows("Live status unavailable") and shows("Redemption status unavailable") and not shows("1800200211"),
    "disconnected window retained old character credit or failed to identify the unavailable live status")
  harness.width, harness.height = 420, 900
  click("##journal-route-1")
  check(shows("Gravekeeper Vhal") and shows("Personal status and roster eligibility are unavailable."),
    "narrow offline journal lost its packaged guide or unavailable state")
  check(guideColumns == 1 and #guideWeights == 1 and guideWeights[1] == 1,
    "narrow guardian guide retained side-by-side columns")
  click("##journal-route-4")
  frame()
  check(shows("Lord Inquisitor Seru") and not shows("unavailable"), "disconnect disabled the native guide library")

  local offlineSnapshots, offlineSelections = snapshots, #selected
  combo = "##journal-featured"
  click("##journal-featured-tofs_group")
  check(shows("The Seven Silences") and shows("3–6 characters") and shows("Keeper Avel Rimeglass"),
    "narrow featured menu did not open the group guide with access requirements")
  click("##journal-featured-tofs_raid")
  check(shows("The Unwritten Hour") and shows("12–36 characters"), "ToFS raid reused group access requirements")
  click("##journal-featured-broken_accord")
  check(shows("The Broken Accord") and shows("Kreizenn"), "Broken Accord omitted raid encounter links")
  combo = nil
  click("##journal-guide:featured:north-tov-broken-accord-kreizenn")
  frame()
  check(shows("Kreizenn") and shows("tribute drakes"), "featured encounter link did not open its full guide")
  click("##journal-guide-back")
  frame()
  check(shows("The Broken Accord"), "featured encounter did not return to its previous page")
  combo = "##journal-featured"
  click("##journal-featured-firefell")
  check(shows("The Ember Chronicle") and shows("Chronicler Sihra") and shows("Saved run"),
    "Firefell omitted its entrance or chapter information")
  click("##journal-featured-sleepers")
  combo = nil
  frame()
  check(shows("Public Tomb\nUnavailable") and shows("Sentinel of the Ages") and not shows("Your saved run"),
    "offline Sleeper page guessed the public phase or hid the guide")
  check(snapshots == offlineSnapshots and #selected == offlineSelections, "offline featured navigation made live requests")

  harness.width, harness.height = 1280, 900
  state.generation, state.revision, state.connected = "3", "3", true
  frame()
  check(selected[#selected] == "sleepers:0" and shows("Public Tomb\n2.0") and
    shows("Your guild’s next Static Mode request\n1.0") and shows("Your saved run\n2.0") and shows("2h left"),
    "Sleeper page conflated the public, next guild, or saved phase")
  click("Refresh##journal-status")
  check(refreshed[#refreshed] == "sleepers:0", "Sleeper refresh did not target phase status")
  harness.frame = harness.frame + 61 * 60
  frame()
  check(not shows("Your saved run"), "Sleeper page displayed an expired saved run")
  data["sleepers:0"] = {serverNow = 2000, nextPhase = "1.0", rotation = "guildless"}
  state.revision = "4"
  frame()
  check(shows("Public Tomb\nUnavailable") and shows("Your next Static Mode request\n1.0"),
    "partial Sleeper status guessed the public version or implied guild membership")
  combo = "##journal-featured"
  click("##journal-featured-broken_accord")
  combo = nil
  frame()
  check(selected[#selected] == "broken_accord:0" and shows("Combat tuning") and shows("24–36 level-60 characters")
    and shows("This character needs Velious access."), "Broken Accord missed live entry requirements or static tuning")
  click("Roster##journal-tab")
  check(shows("Ask the roster leader") and not shows("Assigned Group Member"), "Broken Accord exposed another roster")
  click("Overview##journal-tab")
  local beforeGuideSnapshots, beforeGuideSelections = snapshots, #selected
  click("##journal-guide:featured:north-tov-broken-accord-kreizenn")
  frame()
  check(shows("tribute drakes") and snapshots == beforeGuideSnapshots and #selected == beforeGuideSelections,
    "opening a published guide issued another live query")
  combo = "##journal-featured"
  click("##journal-featured-tofs_group")
  check(selected[#selected] == "tofs_group:0" and shows("Rejoin your saved run") and shows("2h left") and shows("6h left"),
    "ToFS Group did not separate active rejoin from the replay lockout")
  click("Roster##journal-tab")
  check(shows("Assigned Group Member") and shows("Can rejoin"), "ToFS Group did not show assigned roster eligibility")
  click("##journal-featured-tofs_raid")
  check(selected[#selected] == "tofs_raid:0" and shows("2 of 3 held") and shows("Already paid")
    and shows("Resume in DZ Manager"), "ToFS Raid lost materials, paid retry, or distinct restore instructions")
  click("##journal-featured-firefell")
  combo = nil
  frame()
  check(selected[#selected] == "firefell:0" and shows("20m left") and shows("12h left")
    and shows("Chapter access"), "Firefell lost request, completion, or chapter state")
  click("Roster##journal-tab")
  check(shows("Velious Tester") and not shows("Luclin Tester"), "Firefell roster mixed chapter results")
  combo = "##journal-roster-chapter"
  click("Luclin")
  combo = nil
  check(shows("Luclin Tester") and not shows("Velious Tester"), "Firefell roster chapter selection ignored the new chapter")
  harness.close = true
  frame()
  check(closed == 1 and not state.shown, "native window close did not retire the view")
  harness.finish()
end

print("Expedition Journal UI PASS " .. checks .. " checks")
