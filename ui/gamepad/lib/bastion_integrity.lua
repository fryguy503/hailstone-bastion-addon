-- SPDX-License-Identifier: MIT
local M={}
local details=false
function M.draw(p,C)
  local state=p.integrity and p.integrity("state")
  C.section("Client verification")
  if not state then C.note("Unavailable in this client build.","muted");return end
  local legacy=not state.native_protocol and(state.legacy_measured or state.legacy_submitted or state.status=="Legacy DLL report queued"or state.status=="Legacy DLL report submitted")
  if legacy then
    C.field("Legacy report",state.legacy_submitted and"Submitted"or state.legacy_measured and(state.status=="Legacy DLL report queued"and"Measured; waiting to send"or state.status)or state.status or"Unavailable")
    C.field("Server","Unconfirmed (no acknowledgement)","muted")
    C.tooltip("The legacy report sends the loaded bastion.dll checksum. This protocol has no server acknowledgement; submitted means the host accepted the send request.")
  else
    local report=state.sent and"Sent"or state.measured and(state.status=="Report queued"and"Measured; waiting to send"or state.status)or state.status
    C.field("Report",report or"Unavailable")
    C.field("Server",state.accepted and"Accepted"or state.sent and(state.status=="Report sent"and"Pending"or state.status)or"Not received",state.accepted and"good"or"muted")
    C.tooltip("Bastion checks whether the executable and addon are an approved pair.")
  end
  if state.reason and state.reason~=""and state.status~="Waiting for server challenge"then C.note(state.reason,"muted")end
  if state.measured or legacy and state.legacy_measured then
    if C.button(details and"Hide binary details"or"Binary details",0,false,true)then details=not details end
    if details then
      if legacy then
        C.field("bastion.dll",tostring(state.legacy_addon_bytes or"0").." bytes")
        C.field("FNV63 checksum",tostring(state.legacy_checksum or"Unavailable"))
      else
        C.field("EXE / addon",tostring(state.executable_bytes or"0").." / "..tostring(state.addon_bytes or"0").." bytes")
      end
      C.field("Version",state.addon_version or"Unavailable")
    end
  end
end
return M
