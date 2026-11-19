-- SPDX-License-Identifier: MIT
local M={}
function M.draw(p,R)
  local s=p.twist and p.twist("state")
  if not s or not s.active then return end
  local C=R.C
  C.section("Song twisting")
  local settings=s.settings or{}
  local quiet,changed=ui.Checkbox("Quiet twist messages",settings.quiet==true or settings.quiet==1)
  if changed then p.twist("set","quiet",quiet and 1 or 0)end
  local nonbard,changedClass=ui.Checkbox("Allow twisting on other classes",settings.allow_nonbard==true or settings.allow_nonbard==1)
  if changedClass then p.twist("set","allow_nonbard",nonbard and 1 or 0)end
  C.tooltip("Uses your memorized gems. Normal casting permissions still apply.")
  C.field("Twist",s.running and(s.hold and s.hold>0 and"Holding gem "..s.hold or"Running")or"Stopped")
  if s.queue and s.queue~=""then C.note("Gems: "..s.queue,"muted")end
  R.actions({
    {label="Resume twist",enabled=not s.running and s.queue~=nil and s.queue~="",run=function()p.twist("command","start")end},
    {label="Stop twist",enabled=s.running==true,run=function()p.twist("command","stop")end},
  })
  C.note("/twist 1 2 3 starts a queue. /twist help lists timing and controls.","muted")
  local message=s.error and s.error~=""and s.error or s.message
  if message and message~=""then C.note(message,s.error and s.error~=""and"warn"or"muted")end
end
return M
