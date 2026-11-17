local Host=dofile("lib/bastion_host.lua")
local R=dofile("lib/citadel_received.lua")
local C=R.C
BASTION_UI=BASTION_UI or {}
local selected,review,revision
ui.window("Bastion Saved Expeditions",function()
  local p=Host.Plugin("bastion")
  local visibility=p and p.saved_expeditions and p.saved_expeditions("visibility")
  if not visibility or visibility.active~=true then
    BASTION_UI.saved_expeditions=false;revision=nil;selected=nil;review=nil
    return
  end
  if not visibility.shown and not BASTION_UI.saved_expeditions then return end
  local s=p.saved_expeditions("state");if not s then return end
  if revision~=s.revision then selected=nil;review=nil;revision=s.revision end
  if not s.shown and not BASTION_UI.saved_expeditions then return end
  local visible,open=R.begin("Bastion Saved Expeditions",1040,710);BASTION_UI.saved_expeditions=open
  if not open then p.saved_expeditions("close");review=nil end
  if visible then
    C.header("Your Saved Expeditions","Return to an expedition whose saved membership is still available.",#(s.entries or {}).." saved")
    C.card("saved-list",0,R.height_for(290,240),function()
      R.table("saved-expeditions",{{"Expedition",3},{"Time left",1},{"Members",0.8},{"Status",1.4}},function()
        for _,e in ipairs(s.entries or {})do
          ui.TableNextRow();R.cell()
          if ui.Selectable(e.name.."##saved"..e.dz_id..e.uuid,selected==e.dz_id)then selected=e.dz_id;review=nil end
          C.tooltip(e.name.."\nLeader: "..e.leader)
          R.cell();C.note(R.time(e.remaining_seconds),tonumber(e.remaining_seconds or 0)<3600 and"warn"or"text")
          R.cell();C.note(e.members.." / "..e.maximum)
          R.cell();C.note(e.eligibility,e.can_rejoin and"good"or"muted")
        end
      end)
      if #(s.entries or {})==0 then
        C.note(s.pending and"Looking for saved expeditions..."or(s.fresh and"No unexpired saved expeditions."or"Refresh to view your saved expeditions."),"muted")
      end
    end)
    local row;for _,e in ipairs(s.entries or {})do if e.dz_id==selected then row=e;break end end
    if row then
      C.section(row.name,"Led by "..row.leader)
      R.table("saved-detail",{"Destination","Membership","Time remaining"},function()
        ui.TableNextRow();R.cell();C.note("Zone "..row.zone_id.."  |  Version "..row.zone_version);C.note("Instance "..row.instance_id,"muted")
        R.cell();C.note(row.members.." / "..row.maximum.." members");C.note(row.eligibility,row.can_rejoin and"good"or"warn")
        R.cell();C.note(R.time(row.remaining_seconds),"gold")
      end)
      C.note("Rejoin this expedition, then enter through its usual NPC or entrance.","muted")
    else
      C.section("Expedition details")
      C.note("Select a saved expedition to view its leader, remaining time and rejoin availability.","muted")
    end
    if review and(not row or review.revision~=s.revision or review.id~=row.dz_id or review.uuid~=row.uuid or not row.can_rejoin or not s.fresh or s.pending)then review=nil end
    if review then
      R.review("Rejoin "..row.name.."?","Your saved membership will be restored. Enter through the expedition's usual NPC or entrance.",function()
        p.saved_expeditions("resume",review.revision,review.id,review.uuid);review=nil
      end,function()review=nil end,"Yes, rejoin","Cancel rejoin")
    else
      ui.Separator();R.status(s,"Saved expeditions updated.")
      R.actions({
        {label="Review rejoin",width=180*R.s,primary=true,enabled=row and row.can_rejoin and s.fresh and not s.pending,
          run=function()review={revision=s.revision,id=row.dz_id,uuid=row.uuid}end},
        {label="Refresh saved expeditions",enabled=s.available and not s.pending,run=function()p.saved_expeditions("refresh");review=nil end},
        {label="Close expeditions",enabled=true,run=function()p.saved_expeditions("close");BASTION_UI.saved_expeditions=false;review=nil end},
      })
    end
  end
  if R.finish()then p.saved_expeditions("close");BASTION_UI.saved_expeditions=false;review=nil end
end)
