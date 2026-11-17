local root=ADDON_ROOT or(arg and arg[1])or"."
local checks=0
local function check(value,why)checks=checks+1;assert(value,why)end
local function proxy(value)
  return setmetatable({}, {
    __index=function(_,name)return value[name]end,
    __newindex=function()error("Host proxy was mutated")end,
    __pairs=function()error("Host proxy was enumerated")end,
    __metatable=false,
  })
end
local function load(pack,environment)
  environment.type=type
  return assert(loadfile(root.."/ui/"..pack.."/lib/bastion_host.lua","t",environment))()
end
for _,pack in ipairs({"modern","gamepad"})do
  -- No eq namespace exists: exercise every canonical method and preserve its exact returns.
  local reads,icons,animations=0,0,0
  local current={generation=1}
  local plugins={Get=function(name)check(name=="bastion","Canonical plugin name changed");reads=reads+1;return current end}
  local assets={ItemIcon=function(icon)
      check(icon==123,"Canonical item icon changed");icons=icons+1;return "texture",nil,0.25,1,0.75
    end,
    Animation=function(name)
      check(name=="CityMap","Canonical animation name changed");animations=animations+1;return "map",0,0,1,1,256,128
    end}
  local environment={hs={UI={plugins=plugins,assets=assets}}}
  local host=load(pack,environment)
  check(host.Plugin("bastion")==current and reads==1,"hs-only plugin lookup failed")
  local texture,u0,v0,u1,v1=host.ItemIcon(123)
  check(texture=="texture"and u0==nil and v0==0.25 and u1==1 and v1==0.75 and icons==1,
    "Item icon results lost nil values or UV coordinates")
  local art,_,_,_,_,width,height=host.UiAnimation("CityMap")
  check(art=="map"and width==256 and height==128 and animations==1,"Animation dimensions were lost")

  -- A present canonical function owns nil results too; they must not trigger a legacy retry.
  local legacyCalls=0
  local legacy={Plugin=function()legacyCalls=legacyCalls+1;return "legacy"end,
    ItemIcon=function()legacyCalls=legacyCalls+1;return "old texture"end,
    UiAnimation=function()legacyCalls=legacyCalls+1;return "old map"end}
  environment.eq=legacy
  check(host.Plugin("bastion")==current and legacyCalls==0,"Legacy plugin overrode the canonical API")
  current=nil
  check(host.Plugin("bastion")==nil and legacyCalls==0,"Unloaded canonical plugin fell back to a stale provider")
  assets.ItemIcon=function()return nil end;assets.Animation=function()return nil end
  check(host.ItemIcon(123)==nil and host.UiAnimation("CityMap")==nil and legacyCalls==0,
    "Missing canonical artwork retried an unrelated legacy provider")
  current={generation=2}
  check(host.Plugin("bastion")==current and reads==4,"Plugin lookup cached an instance across unload/reload")
  plugins.Get=function()return "replacement getter"end
  check(host.Plugin("bastion")=="replacement getter","Plugin lookup cached a retired host method")

  -- Real plugin environments expose nested methods through read-only __index proxies.
  local api={plugins=proxy({Get=function(name)return "proxy:"..name end}),
    assets=proxy({ItemIcon=function(icon)return "proxy icon",icon end,Animation=function(name)return "proxy art",name end})}
  local backing={UI=proxy(api)}
  environment={hs=proxy(backing),eq=proxy(legacy)}
  host=load(pack,environment)
  check(host.Plugin("bastion")=="proxy:bastion","Canonical proxy lookup used raw fields")
  local image,value=host.ItemIcon(44);check(image=="proxy icon"and value==44,"Item proxy lookup failed")
  image,value=host.UiAnimation("Any");check(image=="proxy art"and value=="Any","Animation proxy lookup failed")
  check(next(environment.hs)==nil and next(backing.UI)==nil,"Adapter wrote a host-wide alias")
  api.plugins=proxy({Get=function()return "new proxy"end})
  check(host.Plugin("bastion")=="new proxy","Adapter cached a retired canonical service proxy")

  -- The companion's older eq API remains supported, including guarded proxy lookups.
  local oldCurrent={generation=3}
  local old={Plugin=function(name)check(name=="bastion","Legacy plugin name changed");return oldCurrent end,
    ItemIcon=function(id)return "legacy icon",id end,UiAnimation=function(name)return "legacy art",name end}
  environment={eq=proxy(old)};host=load(pack,environment)
  check(host.Plugin("bastion")==oldCurrent,"Legacy-only plugin lookup failed")
  image,value=host.ItemIcon(77);check(image=="legacy icon"and value==77,"Legacy item icon failed")
  image,value=host.UiAnimation("Any");check(image=="legacy art"and value=="Any","Legacy animation failed")
  oldCurrent=nil;check(host.Plugin("bastion")==nil,"Legacy plugin remained cached after unload")
  oldCurrent={generation=4};check(host.Plugin("bastion")==oldCurrent,"Legacy reload returned a stale instance")
  environment.hs={UI={plugins={},assets={ItemIcon="unsupported",Animation=false}}}
  check(host.Plugin("bastion")==oldCurrent,"Missing canonical method did not use the companion fallback")
  check(host.ItemIcon(77)=="legacy icon"and host.UiAnimation("Any")=="legacy art","Mistyped optional methods were called")

  for _,missing in ipairs({{}, {hs={}}, {hs={UI={}}}, {hs={UI={plugins={},assets={}}}},
      {hs=false,eq=false}, {hs={UI={plugins=false,assets="unsupported"}}}, {eq={Plugin=true,ItemIcon=7}}})do
    host=load(pack,missing)
    check(host.Plugin("bastion")==nil,"Missing plugin method raised or fabricated an instance")
    check(host.ItemIcon(1)==nil and host.UiAnimation("Absent")==nil,"Missing optional art method raised or fabricated artwork")
  end
end
local function contents(pack)
  local file=assert(io.open(root.."/ui/"..pack.."/lib/bastion_host.lua","rb"))
  local value=file:read("a");file:close();return value
end
check(contents("modern")==contents("gamepad"),"The host adapters differ between UI packs")
print("Host adapter UI PASS "..checks.." checks (hs-only, proxies and legacy fallback)")
