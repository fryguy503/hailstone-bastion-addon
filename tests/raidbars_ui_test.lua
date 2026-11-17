local root=ADDON_ROOT or(arg and arg[1])or"."
local count=0;local function check(v,why)count=count+1;assert(v,why)end
local Host=dofile(root.."/tests/raidbars_ui_host.lua")
for _,pack in ipairs({"modern","gamepad"})do
 local calls={};local state={available=1,error="",in_zone=1,in_raid=1,can_manage=0,manager_supported=1,manager_review={active=0},
  roster_members={{name="MiRa",self=0},{name="ReLaYn",self=0},{name="Pet",pet=1}},roster_revision="18446744073709551615",settings={enabled=1,edit=0,clickable=1,clickthrough=0,show_all=1,group_sort=1,mana=0,
  background=80,bar_width=150,bar_height=28,pet_width=0,pet_height=12,left=20,top=30,right=0,bottom=0,rows=12,columns=6,always=0,never=0,filter=0,priority="CLR WAR",show_empty=0,manage=0,class_colors=1,show_self=1,filter_groups=0},manage_supported=1,rows={
   {name="Known zero",spawn_id=2,incarnation="9007199254740993",class_id=2,group=1,health_known=1,health=0,mana_known=0,
    pets={{name="Observed pet",spawn_id=8,incarnation="80",pet=1,health_known=0}}},
   {name="Off-zone member",spawn_id=0,incarnation="0",group=2,health_known=0,pets={}}}}
 local fullReads,statusReads=0,0
 local p={raidbars=function(op,...)
  if op=="state"then fullReads=fullReads+1;return state end
  if op=="status"then statusReads=statusReads+1;return state end
  calls[#calls+1]={op,...};if op=="set"then state.settings[select(1,...)]=select(2,...)end;return{ok=1,error=""}
 end}
 local h=Host.new(root,pack,check,p);h.load("raidbars.lua")
 state.settings.enabled=0;BASTION_UI.raidbars=false
 for _=1,120 do h.frame("Bastion Raid Bars")end
 check(fullReads==0 and statusReads==120 and #calls==0,"Disabled overlay requested full snapshots or emitted actions")
 state.settings.enabled=1;state.in_zone=0;h.frame("Bastion Raid Bars")
 check(fullReads==0 and #h.bars==0 and #calls==0,"Locally zoning overlay sampled or rendered a stale InGame raid snapshot")
 state.in_zone=1
 state.settings.enabled=1;BASTION_UI.raidbars=true
 h.frame("Bastion Raid Bar Settings");check(#calls==0 and h.texts["Raid preview"],"Preview emitted action or missing")
 h.boxes["Show raid bars##rb:enabled"]=false;h.frame("Bastion Raid Bar Settings")
 check(calls[#calls][2]=="enabled"and calls[#calls][3]==0,"Overlay toggle bound wrong setting")
 h.buttons["Layout##rb-page"]=true;local before=#calls;h.frame("Bastion Raid Bar Settings")
 check(#calls==before and h.rendered["##rb:bar_width"]and h.rendered["##rb:pet_height"],"Layout page lost dimensions or auto-saved on navigation")
 h.sliders["##rb:rows"]=6;h.frame("Bastion Raid Bar Settings");check(calls[#calls][2]=="rows"and calls[#calls][3]==6,"Grid rows field not wired")
 h.buttons["Class rules##rb-page"]=true;h.frame("Bastion Raid Bar Settings")
 h.buttons["Never show##rb-rule"]=true;h.frame("Bastion Raid Bar Settings")
 h.boxes["CLR##rbclass:never:2"]=true;h.frame("Bastion Raid Bar Settings")
 check(calls[#calls][2]=="never"and calls[#calls][3]==2,"Class rule did not preserve exact bitmask")
 h.combos["##rb-priority-preset"]=true;h.selects["Healers first##rbpreset"]=true;h.frame("Bastion Raid Bar Settings");h.combos={}
 check(calls[#calls][2]=="priority"and calls[#calls][3]=="CLR DRU SHM","Controller priority preset not wired")
 state.settings.enabled=1;before=#calls;h.frame("Bastion Raid Bars")
 check(#calls==before and #h.bars==3 and h.bars[1].value==0,"Overlay auto-targeted or known zero lost")
 local overlayWindow="Bastion raid bars###bastionraidbars"
 local baseFlags=ui.WindowFlags.NoSavedSettings|ui.WindowFlags.NoCollapse|ui.WindowFlags.NoResize|
  ui.WindowFlags.NoTitleBar|ui.WindowFlags.NoMove|ui.WindowFlags.NoFocusOnAppearing
 check(ui.WindowFlags.HorizontalScrollbar==nil and ui.WindowFlags.UnknownFlag==nil,"WindowFlags fixture invented unsupported exports")
 check(h.windowFlags[overlayWindow]==baseFlags,"Overlay did not draw with the current host's missing optional scrollbar flag")
 ui.WindowFlags.HorizontalScrollbar=1<<11
 h.frame("Bastion Raid Bars")
 check(h.windowFlags[overlayWindow]==(baseFlags|ui.WindowFlags.HorizontalScrollbar)and #h.bars==3 and #calls==before,
  "Available named horizontal scrollbar was not preserved or drawing emitted an action")
 ui.WindowFlags.HorizontalScrollbar=nil
 h.frame("Bastion Raid Bars")
 check(h.windowFlags[overlayWindow]==baseFlags and #h.bars==3 and #calls==before,
  "Overlay failed to draw when the optional host flag was absent again")
 local unavailable=false;for _,text in ipairs(h.drawtexts)do if text:find("unavailable",1,true)then unavailable=true end end
 check(unavailable,"Unknown health did not render unavailable")
 h.buttons["##raidactor:2:9007199254740993"]=true;h.frame("Bastion Raid Bars");local c=calls[#calls]
 check(c[1]=="select"and c[2]==state.roster_revision and c[3]==2 and c[4]=="9007199254740993","Manual target lost revision/identity")
 before=#calls;h.buttons["##raidactor:0:0:Off-zone member"]=true;h.frame("Bastion Raid Bars");check(#calls==before,"Unknown actor targetable")
 state.settings.show_empty=1
 local Overlay=dofile("lib/raidbar_overlay.lua");local cards=Overlay.cards(state)
 check(#cards==12 and #cards[1].rows==6 and cards[1].rows[6].empty and cards[12].label=="Group 12  0/6","Group cards lost headings or six-slot placeholders")
 state.settings.manage=1;h.io.ctrl=true;h.buttons["##raidactor:2:9007199254740993"]=true;before=#calls;h.frame("Bastion Raid Bars")
 check(#calls==before,"Ctrl selection sent a target or move before destination")
 h.buttons["Group 2  1/6##raidgroup:2"]=true;h.frame("Bastion Raid Bars");local move=calls[#calls]
 check(move[1]=="action"and move[2]==state.roster_revision and move[3]=="Known zero"and move[4]==1 and move[5]==2,"Ctrl destination lost exact member/revision/group")
 h.io.ctrl=false;h.io.alt=true;h.buttons["##raidactor:2:9007199254740993"]=true;h.frame("Bastion Raid Bars");check(calls[#calls][1]=="action"and calls[#calls][5]==0,"Alt-click did not ungroup")
 h.io.alt=false;h.io.shift=true;h.buttons["##raidactor:2:9007199254740993"]=true;h.frame("Bastion Raid Bars");check(calls[#calls][4]==2,"Shift-click did not request subgroup promotion")
 h.io.shift=false
 h.buttons["Manage##rb-page"]=true;h.frame("Bastion Raid Bar Settings")
 before=#calls;h.frame("Bastion Raid Bar Settings")
 check(#calls==before,"Opening the manager controls sent a request")
 state.settings.manage=0;h.buttons["List raid managers##rb-manager-list"]=true;h.frame("Bastion Raid Bar Settings")
 check(calls[#calls][1]=="manager"and calls[#calls][2]==state.roster_revision and calls[#calls][3]=="list",
  "List managers lost the exact roster revision or required mutation mode")
 check(h.texts["Manager list requested. Read the server reply in chat."],"List did not explain where the server reply appears")
 before=#calls;h.buttons["Review grant##rb-manager-grant"]=true;h.frame("Bastion Raid Bar Settings")
 check(#calls==before,"Disabled raid management reviewed a grant")
 state.settings.manage=1;h.buttons["Review grant##rb-manager-grant"]=true;h.frame("Bastion Raid Bar Settings")
 check(calls[#calls][3]=="review_add"and calls[#calls][4]=="MiRa"and not h.rendered["Confirm grant##rb-manager-confirm"],
  "Grant skipped the separate review step or lost canonical character case")
 state.manager_review={active=1,name="MiRa",action="add",roster_revision=state.roster_revision}
 before=#calls;h.frame("Bastion Raid Bar Settings")
 check(#calls==before and h.texts["Grant raid management to MiRa?"],"Grant review auto-submitted or hid the exact character")
 h.buttons["Confirm grant##rb-manager-confirm"]=true;h.frame("Bastion Raid Bar Settings")
 check(calls[#calls][1]=="manager"and calls[#calls][3]=="confirm_add"and calls[#calls][4]=="MiRa"and state.manager_review==nil,
  "Explicit grant confirmation did not consume the current review")
 check(h.texts["Request sent. Check server chat for confirmation."]and state.can_manage==0,"Grant invented a local permission or claimed server acceptance")
 state.manager_review={active=1,name="MiRa",action="remove",roster_revision=state.roster_revision}
 h.buttons["Cancel##rb-manager-cancel"]=true;h.frame("Bastion Raid Bar Settings")
 check(calls[#calls][3]=="cancel"and state.manager_review==nil,"Cancel sent a revoke instead of clearing review")
 state.manager_review={active=1,name="MiRa",action="add",roster_revision=state.roster_revision}
 h.combos["##rb-manager-member"]=true;h.selects["ReLaYn##rb-manager-member"]=true;h.frame("Bastion Raid Bar Settings");h.combos={}
 check(calls[#calls][3]=="cancel"and state.manager_review==nil,"Changing the delegation target retained an old confirmation")
 h.buttons["Review revoke##rb-manager-revoke"]=true;h.frame("Bastion Raid Bar Settings")
 check(calls[#calls][3]=="review_remove"and calls[#calls][4]=="ReLaYn","Unfiltered off-zone delegation target was unavailable")
 state.manager_review={active=1,name="ReLaYn",action="remove",roster_revision=state.roster_revision}
 h.buttons["Confirm revoke##rb-manager-confirm"]=true;h.frame("Bastion Raid Bar Settings")
 check(calls[#calls][3]=="confirm_remove"and calls[#calls][4]=="ReLaYn","Explicit revoke was not wired with preserved case")
 local revision=state.roster_revision
 state.manager_review={active=1,name="ReLaYn",action="add",roster_revision=revision};state.roster_revision="18446744073709551614"
 before=#calls;h.buttons["Confirm grant##rb-manager-confirm"]=true;h.frame("Bastion Raid Bar Settings")
 check(#calls==before and not h.rendered["Confirm grant##rb-manager-confirm"],"Stale roster review remained confirmable")
 h.buttons={};state.roster_revision=revision;state.manager_review={active=1,name="ReLaYn",action="add",roster_revision=revision};state.manager_supported=0
 before=#calls;h.buttons["List raid managers##rb-manager-list"]=true;h.buttons["Review grant##rb-manager-grant"]=true
 h.buttons["Confirm grant##rb-manager-confirm"]=true;h.frame("Bastion Raid Bar Settings")
 check(#calls==before and not h.rendered["Confirm grant##rb-manager-confirm"],"Revoked chat permission left delegation or a pending confirmation interactive")
 h.buttons={};state.manager_supported=1;state.manager_review=nil
 local roster,shown=state.roster_members,state.rows;state.roster_members={};state.rows={};state.in_raid=0
 before=#calls;h.buttons["List raid managers##rb-manager-list"]=true;h.frame("Bastion Raid Bar Settings")
 check(#calls==before and h.texts["Join a raid to delegate management."],"Out-of-raid delegation remained interactive")
 h.buttons={};state.roster_members=roster;state.rows=shown;state.in_raid=1
 h.combos["##rb-manage-member"]=true;h.selects["Off-zone member##rb-member"]=true;h.frame("Bastion Raid Bar Settings");h.combos={}
 h.buttons["Group 3##rb-move"]=true;h.frame("Bastion Raid Bar Settings")
 check(calls[#calls][1]=="action"and calls[#calls][3]=="Off-zone member"and calls[#calls][5]==3,"Controller-friendly management lost off-zone member")
 state.settings.edit=1;h.frame("Bastion Raid Bars")
 check(calls[#calls][1]=="set"and(state.settings.right or 0)>0 and(state.settings.bottom or 0)>0,"Edit did not persist resized bounds")
 state.settings.edit=0
 before=#calls
 state.settings.clickthrough=1;h.buttons["##raidactor:2:9007199254740993"]=true;h.frame("Bastion Raid Bars");check(#calls==before,"Clickthrough targeted")
 state.available=0;h.frame("Bastion Raid Bars");check(#calls==before and #h.bars==0,"Retired roster interactive")
 local reads=fullReads;h.frame("Bastion Raid Bar Settings")
 check(not BASTION_UI.raidbars and fullReads==reads,"Retired raid settings fetched a full roster or retained its open flag")
 for _=1,120 do h.frame("Bastion Raid Bar Settings")end
 check(fullReads==reads,"Invisible retired raid settings kept polling snapshots")
 state.available=1;state.in_raid=0;h.frame("Bastion Raid Bar Settings")
 check(fullReads==reads,"Raid settings reopened implicitly after peer restoration")
 BASTION_UI.raidbars=true;h.frame("Bastion Raid Bar Settings")
 check(fullReads==reads+1 and BASTION_UI.raidbars,"In-zone out-of-raid settings were hidden or failed to fetch current data")
 state.in_zone=0;reads=fullReads;h.frame("Bastion Raid Bar Settings")
 check(not BASTION_UI.raidbars and fullReads==reads,"Zoning raid settings sampled a full roster")
 state.in_zone=1;BASTION_UI.raidbars=true
 if pack=="gamepad"then h.close=true;h.frame("Bastion Raid Bar Settings");check(not BASTION_UI.raidbars,"Controller B-close failed")end
 h.restore()
end
print("Raid rich two-pack UI: "..count.." checks passed")
