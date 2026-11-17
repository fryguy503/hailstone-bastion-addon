local C=dofile("lib/citadel_style.lua")
local shapes={"circle","donut","cone","rectangle","line","cross","stack","spread","moveaway","gaze","target","all","palette"}
local labels={"Circle","Donut","Cone","Rectangle","Line","Cross","Stack","Spread","Move away","Gaze","Target","All shapes","Palette"}
local descriptions={
 "An area around the marker's center.","A ring with a clear center.","A directional area spreading outward.",
 "A rectangular area on the ground.","A narrow directional lane.","Two crossing lanes.",
 "A location where players gather.","Separate from nearby players.","Move away from the marked location.",
 "Face away from the indicated source.","A marker attached to the selected actor."}
local roles={"hostile","friendly","neutral","safe","auto"}
local roleLabels={"Hostile","Friendly","Neutral","Safe","Automatic"}
local paletteLabels={"Hostile","Friendly","Neutral","Safe"}
local styles={"solid","striped","checker","gradient","ring","auto"}
local styleLabels={"Solid","Striped","Checker","Gradient","Ring","Automatic"}
local page,shape,role,style,paletteRole,arrows=1,1,1,1,1,false
local function choice(label,values,current)
 ui.SetNextItemWidth(-1)
 if ui.BeginCombo(label,values[current])then
  for i,value in ipairs(values)do if ui.Selectable(value,i==current)then current=i end end
  ui.EndCombo()
 end
 return current
end
local function rgb(color)
 return math.floor(color/65536)%256/255,math.floor(color/256)%256/255,color%256/255
end
local function glyph(name,x,y,r,red,green,blue,selected)
 local function line(x0,y0,x1,y1)ui.DrawLine(x0,y0,x1,y1,red,green,blue,1,2)end
 local function ring(cx,cy,size)ui.DrawCircle(cx,cy,size,red,green,blue,1,2)end
 if selected then ui.DrawCircleFilled(x,y,r*1.3,red,green,blue,0.08)end
 if name=="circle"then ui.DrawCircleFilled(x,y,r,red,green,blue,0.18);ring(x,y,r)
 elseif name=="donut"then ring(x,y,r);ring(x,y,r*0.5)
 elseif name=="cone"then
  for i=0,11 do
   local a=-math.pi*0.84+i*math.pi*0.68/12;local b=-math.pi*0.84+(i+1)*math.pi*0.68/12
   ui.DrawTriangleFilled(x,y+r*0.6,x+math.cos(a)*r*1.6,y+math.sin(a)*r*1.6,x+math.cos(b)*r*1.6,y+math.sin(b)*r*1.6,red,green,blue,0.23)
  end
  line(x,y+r*0.6,x-r*1.35,y-r*0.5);line(x,y+r*0.6,x+r*1.35,y-r*0.5)
 elseif name=="rectangle"or name=="line"then
  local h=name=="line"and r*0.25 or r*0.65
  ui.DrawRectFilled(x-r*1.2,y-h,x+r*1.2,y+h,red,green,blue,0.18,1)
  ui.DrawRect(x-r*1.2,y-h,x+r*1.2,y+h,red,green,blue,1,1,2)
 elseif name=="cross"then
  for _,a in ipairs({0,math.pi*0.5})do
   local dx,dy=math.cos(a)*r,math.sin(a)*r
   line(x-dx,y-dy,x+dx,y+dy)
  end
 elseif name=="stack"then ring(x-r*0.35,y+r*0.2,r*0.65);ring(x+r*0.35,y+r*0.2,r*0.65);ring(x,y-r*0.35,r*0.65)
 elseif name=="spread"or name=="moveaway"then
  ring(x,y,r*0.23)
  for i=0,3 do
   local a=i*math.pi*0.5;local dx,dy=math.cos(a),math.sin(a)
   line(x+dx*r*0.45,y+dy*r*0.45,x+dx*r,y+dy*r)
   line(x+dx*r,y+dy*r,x+dx*r*0.7-dy*r*0.2,y+dy*r*0.7+dx*r*0.2)
   line(x+dx*r,y+dy*r,x+dx*r*0.7+dy*r*0.2,y+dy*r*0.7-dx*r*0.2)
  end
 elseif name=="gaze"then
  for i=0,23 do
   local a=i*math.pi*2/24;local b=(i+1)*math.pi*2/24
   line(x+math.cos(a)*r*1.3,y+math.sin(a)*r*0.65,x+math.cos(b)*r*1.3,y+math.sin(b)*r*0.65)
  end
  ring(x,y,r*0.38)
 else ring(x,y,r*0.65);line(x-r,y,x+r,y);line(x,y-r,x,y+r)end
end
local function previewCommand()
 local command="test "..shapes[shape]
 if shape<12 then command=command.." "..roles[role].." "..styles[style]end
 return command..(arrows and " arrows"or" noarrows")
end
local function gallery(p,state,scale,height)
 C.split("telegraph-shapes",290*scale,height,function()
  C.section("LOCAL PREVIEW")
  C.note("Shape","muted");shape=choice("##telegraph-shape",labels,shape)
  C.note("Disposition","muted");role=choice("##telegraph-role",roleLabels,role)
  C.note("Pattern","muted");style=choice("##telegraph-pattern",styleLabels,style)
  arrows=select(1,ui.Checkbox("Directional arrows",arrows))
  ui.Dummy(0,10*scale)
  local x,y=ui.GetCursorScreenPos();local width=ui.GetContentRegionAvail()
  local color=state.settings[roles[role]]or state.settings.hostile or 0xa8ff4338
  local r,g,b=rgb(color);glyph(shapes[shape],x+width/2,y+55*scale,35*scale,r,g,b,true)
  ui.Dummy(width,110*scale)
  C.note("World previews appear around your character for 12 seconds.","muted")
 end,function()
  C.section("SHAPE GUIDE","Select a shape to preview")
  local width=ui.GetContentRegionAvail();local columns=3;local bw=(width-20*scale)/columns
  local color=state.settings[roles[role]]or state.settings.hostile or 0xa8ff4338
  local red,green,blue=rgb(color)
  for i=1,11 do
   if(i-1)%columns>0 then ui.SameLine()end
   local x,y=ui.GetCursorScreenPos()
   if ui.InvisibleButton("##telegraph-shape-"..i,bw,83*scale)then shape=i end
   local selected=shape==i
   ui.DrawRectFilled(x,y,x+bw,y+83*scale,selected and 0.14 or 0.075,selected and 0.20 or 0.11,selected and 0.24 or 0.15,1,3*scale)
   if selected then ui.DrawRect(x,y,x+bw,y+83*scale,0.96,0.75,0.36,1,3*scale,1)end
   glyph(shapes[i],x+bw/2,y+29*scale,18*scale,red,green,blue,selected)
   local tw=ui.CalcTextSize(labels[i]);ui.DrawText(x+math.max(3*scale,(bw-tw)/2),y+57*scale,0.9,0.94,0.98,1,labels[i])
   C.tooltip(descriptions[i])
  end
  C.note(descriptions[shape]or"Preview the full set of encounter shapes.","muted")
 end)
end
local function appearance(p,state,scale,height)
 C.split("telegraph-appearance",300*scale,height,function()
  C.section("VISIBILITY")
  local enabled,changed=ui.Checkbox("Show optional telegraphs",state.settings.enabled==1)
  if changed then p.telegraphs("set","enabled",enabled and 1 or 0)end
  local unified,uc=ui.Checkbox("Use unified encounter palette",state.settings.unified_colors==1)
  if uc then p.telegraphs("set","unified_colors",unified and 1 or 0)end
  local edges,ec=ui.Checkbox("Fine outlines, clear glyphs and impact clock",state.settings.contrast_edges==1)
  if ec then p.telegraphs("set","contrast_edges",edges and 1 or 0)end
  local edge,ew=ui.SliderInt("Edge width",state.settings.edge_width or 32,10,100)
  if ew then p.telegraphs("set","edge_width",edge)end
  if C.button("Bastion palette",0,true)then p.telegraphs("preset","bastion")end
  ui.SameLine();if C.button("High contrast palette")then p.telegraphs("preset","contrast")end
  ui.Dummy(0,6*scale)
  C.field("Opacity",tostring(state.settings.opacity or 100).."%","gold")
  ui.SetNextItemWidth(-1)
  local opacity,changedOpacity=ui.SliderInt("##telegraph-opacity",state.settings.opacity or 100,0,100)
  if changedOpacity then p.telegraphs("set","opacity",opacity)end
  C.note("Required encounter markers retain a readable minimum opacity.","muted")
  ui.Dummy(0,13*scale);C.section("EDIT PALETTE")
  paletteRole=choice("##telegraph-palette-role",paletteLabels,paletteRole)
  local key=roles[paletteRole];local color=state.settings[key]or 0
  local r,g,b=rgb(color)
  ui.SetNextItemWidth(-1);local nr,rc=ui.SliderInt("Red##telegraph",math.floor(r*255+0.5),0,255)
  ui.SetNextItemWidth(-1);local ng,gc=ui.SliderInt("Green##telegraph",math.floor(g*255+0.5),0,255)
  ui.SetNextItemWidth(-1);local nb,bc=ui.SliderInt("Blue##telegraph",math.floor(b*255+0.5),0,255)
  if rc or gc or bc then p.telegraphs("set",key,math.floor(color/16777216)*16777216+nr*65536+ng*256+nb)end
 end,function()
  C.section("ENCOUNTER COLORS")
  local captions={"Hostile areas and incoming danger","Friendly effects and assistance","Neutral encounter indicators","Safe areas and protective effects"}
  for i=1,4 do
   local key=roles[i];local r,g,b=rgb(state.settings[key]or 0)
   C.gauge(roleLabels[i],1,captions[i],{r,g,b,1},30*scale)
   ui.Dummy(0,8*scale)
  end
  C.note("One visual language for every encounter shape: coral danger, blue support, gold information, mint safety. Fine outlines keep the hazard boundary clear over terrain.","muted")
  C.note("The small central clock counts down to impact. Pulsing quickens in the final two seconds. Markers follow slopes, stairs and low props in every color style.","muted")
 end)
end
return function(p,state,scale,framed)
 if not framed then C.header("ENCOUNTER TELEGRAPHS","Appearance and local shape previews",state.ready and "READY"or"WAITING")end
 page=C.tabs("telegraph-page",{"Shape previews","Appearance","Renderer status"},page)
 local _,remaining=ui.GetContentRegionAvail();local _,lineHeight=ui.CalcTextSize("Ag")
 local height=math.max(150*scale,remaining-math.max(82*scale,lineHeight*2+54*C.scale()))
 if page==1 then gallery(p,state,scale,height)
 elseif page==2 then appearance(p,state,scale,height)
 else C.card("telegraph-status",0,height,function()
  C.section("RENDERER STATUS")
  C.note(state.ready and"Encounter telegraphs are ready."or"Waiting for encounter data and the scene renderer.",state.ready and"good"or"warn")
  C.field("Encounter markers",state.markers or 0);C.field("Local previews",state.local_count or 0)
  ui.Dummy(0,12*scale)
  if C.button("Refresh server markers",0,false,true)then p.telegraphs("command","hello")end
  C.note("Local shape guides and previews are separate from encounter data.","muted")
 end)end
 ui.Separator()
 local bw=(ui.GetContentRegionAvail()-20*scale)/3
 if C.button("Preview shapes",bw,true,true,31*scale)then p.telegraphs("command",previewCommand())end
 ui.SameLine();if C.button("Preview all shapes",bw,false,true,31*scale)then p.telegraphs("command","test all"..(arrows and" arrows"or" noarrows"))end
 ui.SameLine();if C.button("Clear preview",bw,false,true,31*scale)then p.telegraphs("command","clear")end
 if state.error and state.error~=""then C.note(state.error,"warn")
 elseif state.message and state.message~=""then C.note(state.message,"muted")
 else C.note(string.format("%d encounter markers  |  %d local previews",state.markers or 0,state.local_count or 0),"muted")end
end
