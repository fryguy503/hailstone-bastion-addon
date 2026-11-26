-- SPDX-License-Identifier: MIT
local M = {}

local reasons = {
  leader_only = "Ask the roster leader to check eligibility.",
  raid_required = "Assemble a raid to request this mode.",
  group_required = "Assemble a group to request this mode.",
  too_few_members = "The roster is below the minimum size.",
  too_many_members = "The roster exceeds the maximum size.",
  level_requirement = "Minimum level requirement",
  level_spread = "Level range requirement",
  active_shared_task = "Assigned to another shared task",
  active_expedition = "Assigned to another expedition",
  replay_lockout = "Replay lockout",
  request_lockout = "Request lockout",
  event_lockout = "Encounter lockout",
  roster_changed = "Roster changed. Refresh to check again.",
  roster_sealed = "The active run has a sealed roster.",
  records_unavailable = "Some roster records are unavailable.",
  cursor_occupied = "Stow the item on your cursor before entering.",
  admission_pending = "Hail the Archivist to check admission.",
  season_required = "Season 1 must be active.",
  character_season_required = "An active Season 1 character is required.",
  server_velious_required = "The server must have Velious access.",
  personal_velious_required = "This character needs Velious access.",
  progression_unavailable = "Progression records are unavailable.",
  expedition_unavailable = "The expedition is unavailable.",
  completion_lockout = "Completion lockout",
  writ_required = "Blood-Sealed Writ required",
  content_unavailable = "Firefell chapter data is unavailable.",
  chapter_locked = "The chapter is locked for this character.",
  public_chapter_required = "The public chapter must be unlocked.",
  season_mismatch = "The character does not match this chapter’s season.",
  season_inactive = "The chapter’s season is inactive.",
  private_admission_required = "Private chapter admission is required.",
  original_roster_required = "Only original chapter members can return.",
  run_locked = "The run is locked.",
  run_full = "The run is full.",
  encounter_active = "Wait for the current encounter to end.",
}

local function key(mode, id) return mode .. ":" .. tostring(id or 0) end

function M.liveMode(mode)
  return mode == "normal" or mode == "hard" or mode == "vigil" or mode == "copium" or mode == "sleepers"
    or mode == "tofs_group" or mode == "tofs_raid" or mode == "broken_accord" or mode == "firefell"
end

function M.new()
  return {
    mode = "normal", raidMode = "normal", tab = "overview", snapshots = {},
    library = {query = "", zone = "", kind = "", page = 1},
  }
end

function M.reset(view)
  view.selectedKey = nil
  view.generation, view.revision = nil, nil
  view.snapshots = {}
  view.relicId = nil
  view.featuredChapter = nil
  view.refreshRelic = false
end

function M.navigate(view, mode, resetTab)
  view.mode = mode
  view.encounter = nil
  if mode == "normal" or mode == "hard" then view.raidMode = mode end
  if resetTab then view.tab = "overview" end
  view.scroll = true
end

function M.poll(view, window, journal, now)
  if window.generation ~= view.generation or window.connected ~= view.connected then
    M.reset(view)
    view.generation, view.connected = window.generation, window.connected
  end
  if window.revision ~= view.revision then
    view.revision = window.revision
    view.snapshots = {}
  end
  if not window.connected or not M.liveMode(view.mode) or view.encounter then return {}, {} end

  local function snapshot(id)
    local k = key(view.mode, id)
    local result = view.snapshots[k]
    if not result then
      result = journal("snapshot", view.mode, tostring(id or 0)) or {}
      result.sampled = now
      view.snapshots[k] = result
    end
    return result
  end

  local function select(id)
    local k = key(view.mode, id)
    if k ~= view.selectedKey then
      view.selectedKey = k
      journal("select", view.mode, tostring(id or 0))
    end
  end

  if view.mode ~= "copium" then
    select()
    return snapshot(), {}
  end

  local list = snapshot()
  if not list.data or type(list.data.relics) ~= "table" then
    select()
    return list, {}
  end
  local current, held, jelvan = nil, nil, nil
  for _, relic in ipairs(list.data.relics) do
    if relic.relicId == view.relicId then current = relic end
    if not held and (relic.quantity or 0) > 0 then held = relic end
    if relic.relicId == 170009 then jelvan = relic end
  end
  local choice = current or held or jelvan or list.data.relics[1]
  view.relicId = choice and choice.relicId or nil
  if not view.relicId or list.pending then return list, {} end
  select(view.relicId)
  if view.refreshRelic then
    view.refreshRelic = false
    journal("refresh", "copium", tostring(view.relicId))
  end
  return list, snapshot(view.relicId)
end

function M.refresh(view, journal)
  if view.mode == "copium" then view.refreshRelic = true end
  journal("refresh", view.mode, "0")
end

function M.age(snapshot, now)
  return (snapshot.ageMs or 0) / 1000 + math.max(0, now - (snapshot.sampled or now))
end

function M.remaining(snapshot, deadline, now)
  local serverNow = snapshot.data and snapshot.data.serverNow
  if type(deadline) ~= "number" or deadline <= 0 or type(serverNow) ~= "number" or serverNow <= 0 then return nil end
  return deadline - serverNow - math.floor(M.age(snapshot, now))
end

function M.duration(seconds, unknown)
  if not seconds then return unknown or "Unavailable" end
  if seconds <= 0 then return "Expired" end
  local days = math.floor(seconds / 86400)
  local hours = math.floor(seconds % 86400 / 3600)
  local minutes = math.floor(seconds % 3600 / 60)
  local parts = {}
  if days > 0 then parts[#parts + 1] = days .. "d" end
  if hours > 0 then parts[#parts + 1] = hours .. "h" end
  if minutes > 0 then parts[#parts + 1] = minutes .. "m" end
  if #parts == 0 then return "Less than 1m left" end
  return table.concat(parts, " ") .. " left"
end

function M.roster(snapshot, now, supplied)
  local roster = supplied or snapshot.data and snapshot.data.roster
  if not roster then return {state = "unavailable"} end
  if M.age(snapshot, now) >= 30 then return {state = "stale"} end
  return roster
end

function M.reason(value)
  return reasons[value]
end

function M.rejoin(snapshot, now)
  local run = snapshot.data and snapshot.data.run
  if not run or run.state ~= "rejoin" then return false end
  local time = M.remaining(snapshot, run.expiresAt, now)
  return time == nil or time > 0
end

function M.resume(snapshot, now)
  local run = snapshot.data and snapshot.data.run
  if not run or run.state ~= "resume" then return false end
  local time = M.remaining(snapshot, run.expiresAt, now)
  return time == nil or time > 0
end

function M.lockoutLabel(snapshot, lockout, now)
  if not lockout then return "Unavailable" end
  if lockout.state == "clear" then return "Clear" end
  local time = M.remaining(snapshot, lockout.expiresAt, now)
  if lockout.state == "locked" then
    if time and time <= 0 then return "Unavailable" end
    return M.duration(time, "Locked")
  end
  return "Unavailable"
end

function M.runLabels(snapshot, now)
  local run = snapshot.data and snapshot.data.run
  local replay = snapshot.data and snapshot.data.replay
  local runTime = M.remaining(snapshot, run and run.expiresAt, now)
  local saved = "Unavailable"
  if M.rejoin(snapshot, now) then saved = M.duration(runTime, "Available")
  elseif M.resume(snapshot, now) then saved = "Resume in DZ Manager"
  elseif run and run.state == "none" then saved = "No saved run"
  elseif runTime and runTime <= 0 then saved = "Expired"
  elseif run and run.state == "blocked" then saved = M.reason(run.reason) or "Re-entry blocked" end
  return saved, M.lockoutLabel(snapshot, replay, now)
end

function M.featuredUnlock(unlock)
  local state = unlock and unlock.state
  if state == "unlocked" then return "Unlocked", "good" end
  if state == "not_required" then return "None required", "good" end
  if state == "locked" then return M.reason(unlock.reason) or "Locked", "danger" end
  if state == "pending" then return "Pending", "muted" end
  return "Unavailable", "muted"
end

function M.featuredHeadline(snapshot, now)
  local data = snapshot.data
  if not data then
    if snapshot.pending then return "Checking availability", "Reading your character’s status from Bastion." end
    return "Live status unavailable", "Entry requirements and published guides are below."
  end
  local run, unlock, replay, completion = data.run, data.unlock, data.replay, data.completion
  if M.rejoin(snapshot, now) then
    return "Rejoin your saved run", M.reason(run.reason) or "Return to the entry NPC while you are assigned."
  end
  if M.resume(snapshot, now) then return "Resume your saved run", "Open DZ Manager to restore the saved expedition." end
  if run and run.state == "blocked" then
    local remaining = M.remaining(snapshot, run.expiresAt, now)
    if not remaining or remaining > 0 then
      return "Re-entry blocked", M.reason(run.reason) or "Check your saved run at the entry NPC."
    end
  end
  if replay and replay.state == "locked" and M.lockoutLabel(snapshot, replay, now) ~= "Unavailable" then
    return "Fresh request on replay", "Your replay timer applies to a new run."
  end
  if completion and completion.state == "locked" and M.lockoutLabel(snapshot, completion, now) ~= "Unavailable" then
    return "Completion lockout active", "The completion timer applies across Firefell chapters."
  end
  if unlock and unlock.state == "locked" then
    return "Access requirements remain", M.reason(unlock.reason) or "Check the entry requirements below."
  end
  local roster = M.roster(snapshot, now)
  if roster.state == "blocked" then return "Fresh request blocked", M.reason(roster.reason) or "Open Roster to check who can join." end
  if roster.state == "ready" then return "Roster checks clear", "The entry NPC validates the roster when you request." end
  return "Check availability at the entrance", "Open Roster to see the checks Bastion can verify."
end

function M.headline(mode, snapshot, info, now)
  local data = snapshot.data
  if not data and snapshot.pending then
    return "Checking availability", "Reading your character’s status from Bastion."
  elseif not data then
    if mode == "vigil" then return "Collect the four raid relics", "Check group eligibility at the Archivist." end
    return "Check availability at the Archivist", "Personal status and roster eligibility are unavailable."
  end
  if M.rejoin(snapshot, now) then
    local assignment = mode ~= "vigil" and data.run.sealed and ("the sealed " .. info.name .. " raid") or "this run"
    local note = "You are still assigned to " .. assignment .. ". The replay timer applies to fresh requests."
    return "Rejoin your saved run", data.run.reason == "cursor_occupied" and reasons.cursor_occupied or note
  end
  local unlock = data.unlock and data.unlock.state
  local replay = data.replay and data.replay.state
  local _, replayLabel = M.runLabels(snapshot, now)
  local roster = M.roster(snapshot, now)
  if unlock == "pending" then return "Follow up on your request", "The Archivist recorded an offering for this run. Hail to check its status." end
  if replay == "locked" and replayLabel ~= "Unavailable" then return "Fresh request on replay", "Your replay lockout applies to this mode." end
  if unlock == "locked" then
    if mode == "hard" then return "Unlock Bloodbound requests", "The requester needs to redeem a Blood-Sealed Writ with the Archivist." end
    return "Prepare your admission relics", "The raid leader needs all four relics for a first Normal request. Midnight Vigil supplies them."
  end
  if roster.state == "blocked" then return "Fresh request blocked", "The server found a roster blocker. Open Roster for details." end
  if data.run and data.run.state == "none" and replay == "clear" and (unlock == "unlocked" or unlock == "not_required") then
    if roster.state == "ready" and roster.kind == "request" then
      return "Ready to request", "The Archivist confirms admission when you ask."
    end
    return "Personal checks clear", "Ask the Archivist to validate the roster before a fresh request."
  end
  return "Check availability at the Archivist", "Bastion could not verify every personal requirement."
end

function M.relicQuantity(unlock, id)
  for _, item in ipairs(unlock and unlock.relics or {}) do
    if item.id == id then return item.quantity end
  end
end

function M.unlockLabel(unlock, info)
  local state = unlock and unlock.state
  if state == "pending" then return "Offering accepted" end
  if state == "unlocked" then return "Unlocked" end
  if state == "not_required" then return "None required" end
  if state ~= "locked" then return "Unavailable" end
  if info.unlockKind == "writ" then return "Writ required" end
  if not unlock.relics or #unlock.relics == 0 then return "Admission relics required" end
  local held = 0
  for _, relic in ipairs(info.relics) do
    if (M.relicQuantity(unlock, relic.id) or 0) > 0 then held = held + 1 end
  end
  return held .. " of " .. #info.relics .. " relics held"
end

function M.entry(mode, snapshot, info, now)
  if mode == "vigil" then
    return "The group leader says vigil to request. Members say enter vigil to enter or return while assigned. The leader can restore an eligible saved expedition through DZ Manager."
  end
  if M.rejoin(snapshot, now) then
    return "Return to the Archivist of the Eclipse and say " .. info.enterKeyword .. " while you still belong to the shared task."
  end
  local unlock = snapshot.data and snapshot.data.unlock and snapshot.data.unlock.state
  if unlock == "pending" then return "Hail the Archivist to follow up on the recorded offering." end
  if unlock == "locked" then
    if mode == "hard" then return "Give one Blood-Sealed Writ of the Last Eclipse to the Archivist to unlock Bloodbound requests." end
    return "Hand all four different relics to the Archivist together once the raid is assembled."
  end
  local request = table.concat(info.requestKeywords, " or ")
  return "An unlocked leader says " .. request .. " to request. Members say " .. info.enterKeyword .. "."
end

function M.rosterReason(roster, snapshot, featured)
  if reasons[roster.reason] then return reasons[roster.reason] end
  if roster.state == "stale" then return "Refresh to check the current roster." end
  if snapshot.pending and not snapshot.data then return "Checking roster eligibility." end
  if roster.kind == "run" then return "Assigned members can return while the run is active." end
  if roster.members and #roster.members > 0 then return "The server checks the current roster when you request." end
  return (featured and "The entry NPC" or "The Archivist") .. " validates the roster when you request."
end

function M.memberStatus(member, savedRun, snapshot, now)
  local available = member.state == "eligible"
  local blocked = member.state == "blocked"
  local label = reasons[member.reason] or (available and (savedRun and "Can rejoin" or "Pass"))
    or (blocked and "Blocked") or "Unavailable"
  local time = M.remaining(snapshot, member.expiresAt, now)
  if time and blocked then label = label .. " · " .. M.duration(time) end
  return label, available and "good" or blocked and "danger" or "muted"
end

return M
