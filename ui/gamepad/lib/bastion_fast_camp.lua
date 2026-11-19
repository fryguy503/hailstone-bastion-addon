-- SPDX-License-Identifier: MIT
local M={}
function M.draw(p,R)
  local s=p.fast_camp and p.fast_camp("state")
  if not s or not s.active then return end
  local C=R.C
  C.section("Camping")
  ui.BeginDisabled(not s.supported)
  local enabled,changed=ui.Checkbox("Fast camp when offered",s.enabled==true or s.enabled==1)
  ui.EndDisabled()
  if changed and s.supported then p.fast_camp("set","enabled",enabled and 1 or 0)end
  R.actions({
    {label="Request fast camp",enabled=s.supported and not s.armed,run=function()p.fast_camp("start","")end},
    {label="Use normal countdown",enabled=s.armed==true,run=function()p.fast_camp("cancel")end},
  })
  C.note(s.supported and"Server-approved in safe zones; normal countdown elsewhere."
    or"Requires the matching Hailstone update and server permissions.",s.supported and"muted"or"warn")
  local message=s.error and s.error~=""and s.error or s.message
  if message and message~=""and(s.supported or s.error and s.error~="")then C.note(message,s.error and s.error~=""and"warn"or"muted")end
end
return M
