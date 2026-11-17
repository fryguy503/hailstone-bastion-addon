-- Load the actual client pack helpers, with only the native ImGui/gamepad
-- primitives mocked. Tests therefore exercise W.begin/finish and T.frame/fonts.
return function(root,pack,ui,eq,check)
  local client=CLIENT_ROOT or os.getenv("HAILSTONE_CLIENT_UI_ROOT")
  assert(client and client~="","Set HAILSTONE_CLIENT_UI_ROOT to the client repository for real pack-helper tests")
  local oldDofile=dofile
  local h={width=1280,height=720,uiScale=1,frame=0,close=false,focused=0,loaded={},children=0,childSizes={}}
  local styleVars,styleColors,fonts,clips=0,0,0,0
  local wx,wy,ww,wh=0,0,700,600
  local cw,ch,stack=668,568,{}
  local fontScale=1
  local function enum()
    local nextValue=0
    return setmetatable({},{__index=function(t,key)local value=1<<nextValue;nextValue=nextValue+1;rawset(t,key,value);return value end})
  end
  ui.Cond=ui.Cond or enum();ui.Cond.Always=ui.Cond.Always or 2
  ui.WindowFlags=enum();ui.StyleVar=enum();ui.Col=enum();ui.ButtonFlags=enum();ui.TableFlags=enum()
  ui.GetIO=function()return{width=h.width,height=h.height,uiScale=h.uiScale,nav_active=true,nav_visible=true}end
  ui.GetFrameCount=function()return h.frame end
  ui.GetTime=function()return 10+h.frame/60 end
  ui.GetSetting=function()return nil end
  ui.SetSetting=function()error("Panel drawing changed a setting")end
  ui.SetNextWindowSize=function(w,height)
    check(w>0 and height>0 and w<=h.width and height<=h.height,"window exceeds viewport")
    ww,wh=w,height;cw,ch=math.max(1,w-32),math.max(1,height-32)
  end
  local positioned=false
  ui.SetNextWindowPos=function(x,y,condition)
    check(x>=0 and y>=0,"negative window position")
    if not positioned or condition==ui.Cond.Always then wx,wy=x,y;positioned=true end
  end
  ui.GetWindowPos=function()return wx,wy end
  ui.GetWindowSize=function()return ww,wh end
  ui.GetContentRegionAvail=function()return cw,ch end
  ui.SetCursorPos=function()end;ui.SetCursorScreenPos=function()end
  ui.GetCursorScreenPos=function()return wx+16,wy+16 end
  ui.SetNextWindowFocus=function()h.focused=h.focused+1 end
  ui.SetNextItemWidth=function(width)h.nextItemWidth=width;check(width==-1 or width>0,"invalid item width")end
  ui.SetWindowFocus=function()h.focused=h.focused+1 end
  ui.IsWindowFocused=function()return true end
  ui.IsItemHovered=function()return false end;ui.IsItemActive=function()return false end;ui.IsItemFocused=function()return false end
  ui.InvisibleButton=function()return false end
  ui.PushTextWrapPos=function()end;ui.PopTextWrapPos=function()end
  ui.TextColored=function(_,_,_,_,value)if ui.TextWrapped then ui.TextWrapped(value)end end
  ui.SetTooltip=function()end;ui.Separator=function()end
  ui.Indent=function()end;ui.Unindent=function()end
  ui.BeginDisabled=function()end;ui.EndDisabled=function()end
  ui.SmallButton=function(label)return ui.Button(label)end
  local tables={}
  ui.BeginTable=function(id,n)
    tables[#tables+1]={width=cw,columns=n,column=0};return true
  end
  ui.EndTable=function()
    local t=table.remove(tables);check(t~=nil,"table stack underflow");cw=t.width
  end
  ui.TableSetupColumn=function()end;ui.TableHeadersRow=function()end
  ui.TableNextRow=function()local t=tables[#tables];t.column=0 end
  ui.TableNextColumn=function()
    local t=tables[#tables];t.column=t.column+1;check(t.column<=t.columns,"table column overflow")
    cw=math.max(1,t.width/t.columns-8);return true
  end
  ui.CalcTextSize=function(text)return #tostring(text)*8*fontScale,20*fontScale end
  ui.PushFont=function()fonts=fonts+1 end;ui.PopFont=function()fonts=fonts-1;check(fonts>=0,"font stack underflow")end
  ui.SetWindowFontScale=function(value)fontScale=value;check(value>0 and value<=4,"invalid theme font scale")end
  ui.PushStyleVar=function()styleVars=styleVars+1 end
  ui.PopStyleVar=function(n)styleVars=styleVars-(n or 1);check(styleVars>=0,"style-var stack underflow")end
  ui.PushStyleColor=function()styleColors=styleColors+1 end
  ui.PopStyleColor=function(n)styleColors=styleColors-(n or 1);check(styleColors>=0,"style-color stack underflow")end
  ui.PushClipRect=function()clips=clips+1 end;ui.PopClipRect=function()clips=clips-1;check(clips>=0,"clip stack underflow")end
  for _,name in ipairs({"DrawRectFilled","DrawRect","DrawLine","DrawCircleFilled","DrawCircle","DrawTriangleFilled","DrawText","DrawImage","Dummy","SetKeyboardFocusHere"})do ui[name]=function()end end
  h.images={};h.texts={}
  ui.DrawText=function(_,_,_,_,_,_,value)
    h.texts[#h.texts+1]=value
    if ui.TextWrapped then ui.TextWrapped(value)end
  end
  ui.DrawImage=function(texture,x0,y0,x1,y1,u0,v0,u1,v1)
    check(texture~=nil and x1>=x0 and y1>=y0,"invalid image rectangle")
    h.images[#h.images+1]={texture=texture,x0=x0,y0=y0,x1=x1,y1=y1,u0=u0,v0=v0,u1=u1,v1=v1}
  end
  ui.Image=function(texture,w,height,u0,v0,u1,v1)ui.DrawImage(texture,0,0,w,height,u0,v0,u1,v1)end
  local nativeBegin,nativeEnd=ui.Begin,ui.End
  ui.Begin=function(...)
    local visible,open=nativeBegin(...)
    if pack=="modern"and h.close then open=false end
    return visible,open
  end
  ui.End=function(...)return nativeEnd(...)end
  local beginChild,endChild=ui.BeginChild,ui.EndChild
  ui.BeginChild=function(id,width,height,...)
    width,height=width or 0,height or 0
    check(width<=cw+1 and height<=ch+1,"child exceeds available content")
    stack[#stack+1]={cw,ch};h.children=h.children+1
    cw=width>0 and width or math.max(1,cw+width)
    ch=height>0 and height or math.max(1,ch+height)
    h.childSizes[id]={width=cw,height=ch}
    return beginChild(id,width,height,...)
  end
  ui.EndChild=function(...)
    local parent=table.remove(stack);check(parent~=nil,"child stack underflow")
    cw,ch=parent[1],parent[2];return endChild(...)
  end
  eq.gamepad={connected=function()return true end,type=function()return"xbox"end,mode=function()return"menu"end,
    pressed=function(button)return h.close and button=="b"end,glyph=function(button)return button end,
    request_focus=function()h.focused=h.focused+1 end}
  local canonical,apiMode=assert(loadfile(root.."/tests/host_ui_api_mock.lua"))()(eq,check)
  h.api_mode=apiMode
  GPTHEME=nil;GPSTATE=nil
  dofile=function(path)
    if not path:match("^lib/")then return oldDofile(path)end
    local addon=root.."/ui/"..pack.."/"..path
    local file=io.open(addon,"rb")
    if file then file:close();return assert(loadfile(addon))()end
    h.loaded[path]=true
    return assert(loadfile(client.."/ui/"..pack.."/"..path))()
  end
  function h.advance()h.frame=h.frame+1;h.childSizes={}end
  function h.move(x,y)wx,wy=x,y end
  function h.rect()return wx,wy,ww,wh end
  function h.balanced()
    check(#stack==0 and #tables==0 and styleVars==0 and styleColors==0 and fonts==0 and clips==0,"unbalanced actual pack frame")
    h.close=false
  end
  function h.split_layout(R)
    local region,sameLine,card=ui.GetContentRegionAvail,ui.SameLine,R.C.card
    local oldScale,oldUiScale=R.s,h.uiScale;R.s=1;h.uiScale=1
    local width,cards,joined,order
    ui.GetContentRegionAvail=function()return width,400 end
    ui.SameLine=function()joined=joined+1 end
    R.C.card=function(id,w,height,draw)
      cards[#cards+1]={id=id,width=w,height=height};draw()
    end
    for _,available in ipairs({1200,668,440,260})do
      width=available;cards={};joined=0;order={}
      local narrow=available<620
      R.split("responsive-browser",230,320,function(compact)order[#order+1]="left";check(compact==narrow,"left browser used stale layout mode")end,
        function(compact)order[#order+1]="right";check(compact==narrow,"right browser used stale layout mode")end,110)
      check(#cards==2 and table.concat(order,",")=="left,right","responsive browser changed callback order")
      if narrow then
        check(joined==0 and cards[1].width==0 and cards[2].width==0,"narrow browser retained side-by-side clipped columns")
        check(cards[1].height>0 and cards[1].height<=110 and cards[2].height>cards[1].height and
          cards[1].height+cards[2].height+11<=320,"stacked browser exceeded its original total height or squeezed the city list")
      else
        check(joined==1 and cards[1].width==230 and cards[1].height==320 and cards[2].height==320,"wide browser layout changed")
      end
    end
    R.s=oldScale;h.uiScale=oldUiScale;ui.GetContentRegionAvail,ui.SameLine,R.C.card=region,sameLine,card
  end
  function h.footer_layout(R,labels,confirmLabel,cancelLabel)
    local region,sameLine,button=ui.GetContentRegionAvail,ui.SameLine,ui.Button
    local oldScale,oldUiScale=R.s,h.uiScale;R.s=1;h.uiScale=1
    local width,nextInline,row,used,painted,clicked,invoked
    ui.GetContentRegionAvail=function()return width,400 end
    ui.SameLine=function()nextInline=true end
    ui.Button=function(label,w)
      -- C.push uses 9*C.scale padding; the real Gamepad W.begin replaces it
      -- with rounded 12*T.S padding and 10*T.S spacing. Do not assume R.s==UI scale.
      local desktopScale=math.max(0.65,math.min(2,h.uiScale))
      local padding=pack=="gamepad"and 2*math.floor(12*R.s+0.5)or 18*desktopScale
      local spacing=pack=="gamepad"and math.floor(10*R.s+0.5)or 10*desktopScale
      local visibleLabel=label:match("^(.-)##")or label
      check(w>=math.min(width,ui.CalcTextSize(visibleLabel)+padding),"footer width clipped the label's actual pack padding")
      if nextInline then used=used+spacing+w else row=row+1;used=w end
      nextInline=false
      check(used<=width and w>0,"footer button exceeded its row")
      painted[#painted+1]=label
      return label==clicked
    end
    local function measure(available,draw,click)
      width=available;nextInline=false;row=0;used=0;painted={};clicked=click;invoked=0
      draw()
      return row,painted,invoked
    end
    local actions={}
    for _,label in ipairs(labels)do
      actions[#actions+1]={label=label,enabled=true,run=function()invoked=invoked+1 end}
    end
    local rows,paint,requests=measure(1200,function()R.actions(actions)end)
    check(rows==1 and requests==0 and table.concat(paint,"|")==table.concat(labels,"|"),"wide footer changed order or acted on paint")
    rows,paint,requests=measure(260,function()R.actions(actions)end)
    check(rows>1 and requests==0 and table.concat(paint,"|")==table.concat(labels,"|"),"narrow footer did not wrap in original action order")
    rows,paint,requests=measure(260,function()R.actions(actions)end,labels[2])
    check(requests==1,"wrapped footer did not dispatch only the explicit clicked action")
    local function review()
      R.review("Footer review","Explicit confirmation remains required.",function()invoked=invoked+1 end,
        function()invoked=invoked+10 end,confirmLabel,cancelLabel)
    end
    if confirmLabel then
      rows,paint,requests=measure(220,review)
      check(rows==2 and requests==0 and paint[1]==confirmLabel and paint[2]==cancelLabel,"narrow confirmation did not wrap or changed confirmation order")
      rows,paint,requests=measure(1200,review,cancelLabel)
      check(rows==1 and requests==10,"confirmation cancellation changed action or invoked approval")
    end
    for _,scales in ipairs({{0.5,0.5},{2/3,1},{1,1.5}})do
      R.s,h.uiScale=scales[1],scales[2]
      rows,paint,requests=measure(260,function()R.actions(actions)end)
      check(requests==0 and table.concat(paint,"|")==table.concat(labels,"|"),"mismatched design/UI scales changed footer actions")
    end
    R.s=oldScale;h.uiScale=oldUiScale;ui.GetContentRegionAvail,ui.SameLine,ui.Button=region,sameLine,button
    h.balanced()
  end
  function h.finish()
    h.balanced()
    canonical()
    if pack=="gamepad"then
      check(h.loaded["lib/window.lua"]and h.loaded["lib/theme.lua"]and h.focused>0,"real controller frame/focus was bypassed")
    else check(not h.loaded["lib/theme.lua"],"desktop helper loaded mutable core theme")end
    dofile=oldDofile
  end
  return h
end
