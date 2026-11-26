-- SPDX-License-Identifier: MIT
local M = {}

local themes = {
  mistmoore = {
    top = {.208, .119, .151, 1}, bottom = {.137, .095, .124, 1},
    line = {.325, .208, .176, 1}, label = {.949, .780, .533, 1},
    title = {1, .875, .690, 1}, muted = {.762, .719, .687, 1},
    glow = {.489, .254, .201, 1}, near = {.126, .087, .126, 1},
  },
  tofs = {
    top = {.124, .211, .296, 1}, bottom = {.078, .116, .166, 1},
    line = {.225, .413, .527, 1}, label = {.614, .841, .920, 1},
    title = {.842, .946, 1, 1}, muted = {.703, .801, .857, 1},
    glow = {.337, .665, .832, 1}, far = {.145, .277, .379, 1},
    near = {.075, .148, .227, 1}, accent = {.521, .770, .868, .74},
  },
  broken_accord = {
    top = {.237, .192, .139, 1}, bottom = {.125, .099, .087, 1},
    line = {.450, .339, .210, 1}, label = {.950, .779, .497, 1},
    title = {1, .903, .706, 1}, muted = {.806, .756, .674, 1},
    glow = {.786, .555, .311, 1}, far = {.339, .274, .182, 1},
    near = {.130, .114, .105, 1}, accent = {.802, .564, .321, .56},
  },
  firefell = {
    top = {.291, .136, .100, 1}, bottom = {.151, .077, .078, 1},
    line = {.526, .241, .129, 1}, label = {1, .680, .408, 1},
    title = {1, .850, .670, 1}, muted = {.847, .716, .648, 1},
    glow = {.948, .391, .132, 1}, far = {.429, .190, .119, 1},
    near = {.154, .077, .082, 1}, accent = {.975, .466, .181, .83},
  },
  sleepers = {
    top = {.122, .238, .233, 1}, bottom = {.082, .125, .137, 1},
    line = {.253, .440, .385, 1}, label = {.634, .841, .729, 1},
    title = {.853, .960, .865, 1}, muted = {.696, .815, .767, 1},
    glow = {.334, .680, .508, 1}, far = {.175, .306, .282, 1},
    near = {.076, .157, .159, 1}, accent = {.484, .755, .619, .76},
  },
  copium = {
    top = {.233, .194, .158, 1}, bottom = {.127, .108, .109, 1},
    line = {.442, .340, .225, 1}, label = {.926, .760, .509, 1},
    title = {1, .892, .696, 1}, muted = {.818, .759, .684, 1},
    glow = {.706, .501, .287, 1}, far = {.313, .248, .193, 1},
    near = {.130, .108, .111, 1}, accent = {.872, .671, .374, .78},
  },
}

local function rect(x, y, w, h, c)
  ui.DrawRectFilled(x, y, x + w, y + h, c[1], c[2], c[3], c[4])
end

local function tri(ax, ay, bx, by, cx, cy, c)
  if (bx - ax) * (cy - ay) < (by - ay) * (cx - ax) then bx, by, cx, cy = cx, cy, bx, by end
  ui.DrawTriangleFilled(ax, ay, bx, by, cx, cy, c[1], c[2], c[3], c[4])
end

local function stroke(ax, ay, bx, by, c, width)
  ui.DrawLine(ax, ay, bx, by, c[1], c[2], c[3], c[4], width)
end

local function glow(x, y, radius, c, alpha)
  for ring = 5, 1, -1 do
    ui.DrawCircleFilled(x, y, radius * ring / 5, c[1], c[2], c[3], alpha)
  end
end

local function tower(x, base, width, height, roof, c)
  local eaves = base - height
  rect(x, eaves, width, height, c)
  tri(x, eaves, x + width / 2, eaves - roof, x + width, eaves, c)
end

local function mistmoore(x, y, w, h, scale, c)
  local center, middle = x + w * .785, y + h * .38
  local radius = math.min(58 * scale, w * .14)
  for layer = 7, 1, -1 do
    ui.DrawCircleFilled(center, middle, radius + (layer * 22 + 22) * scale,
      c.glow[1], c.glow[2], c.glow[3], .028)
  end
  ui.DrawCircleFilled(center, middle, radius + 21 * scale, .67, .32, .22, .08)
  ui.DrawCircleFilled(center, middle, radius + 11 * scale, .71, .32, .23, .12)
  ui.DrawCircle(center, middle, radius + 4 * scale, .91, .52, .38, .20, 7 * scale)
  ui.DrawCircleFilled(center, middle, radius, .133, .085, .116, 1)
  ui.DrawCircle(center, middle, radius, .94, .64, .43, .92, 2 * scale)
  ui.DrawCircle(center, middle, radius + 1 * scale, 1, .77, .54, .25, 4 * scale)

  local base, left, span = y + h - 1, x + w * .48, w * .52
  rect(left, base - h * .095, span, h * .095, c.near)
  tri(left, base, left + span * .20, base - h * .24, left + span * .52, base, c.near)
  tri(left + span * .52, base, left + span * .91, base - h * .31, left + span, base, c.near)
  tower(left + span * .17, base, span * .115, h * .30, h * .095, c.near)
  tower(left + span * .32, base, span * .13, h * .43, h * .135, c.near)
  tower(left + span * .48, base, span * .09, h * .31, h * .055, c.near)
  tower(left + span * .55, base, span * .16, h * .55, h * .16, c.near)
  tower(left + span * .75, base, span * .12, h * .31, h * .084, c.near)
  tower(left + span * .88, base, span * .08, h * .22, h * .048, c.near)
end

local iceSnow = {.718, .860, .921, .48}
local iceFace = {.172, .314, .426, 1}
local snowSpecks = {{.23, .23}, {.32, .46}, {.51, .18}, {.63, .15}, {.76, .28}, {.87, .22}, {.94, .40}}

local function tofs(x, y, w, h, scale, c)
  local base, left, span = y + h, x + w * .49, w * .51
  local spire = left + span * .59
  glow(spire, y + h * .34, h * .68, c.glow, .10)

  tri(left, base, left + span * .31, y + h * .46, left + span * .61, base, c.far)
  tri(left + span * .53, base, left + span * .83, y + h * .53, x + w, base, c.far)
  tri(left + span * .23, y + h * .57, left + span * .31, y + h * .46, left + span * .42, y + h * .65, iceSnow)
  tri(left + span * .23, base, left + span * .52, y + h * .66, left + span * .91, base, c.near)
  tri(left + span * .57, base, left + span * .94, y + h * .70, x + w, base, c.near)
  rect(left + span * .18, base - h * .08, span * .82, h * .08, c.near)

  tower(spire - span * .079, base - h * .06, span * .158, h * .62, h * .20, c.near)
  tower(spire - span * .117, base - h * .18, span * .064, h * .28, h * .14, c.near)
  tower(spire + span * .065, base - h * .10, span * .068, h * .34, h * .15, c.near)
  tri(spire, y + h * .12, spire + span * .079, y + h * .32, spire, y + h * .34, iceFace)
  rect(spire, y + h * .32, span * .074, h * .58, iceFace)
  for level = 0, 3 do
    local floor = y + h * (.40 + level * .125)
    rect(spire - span * .088, floor, span * .176, h * .021, c.near)
    rect(spire + span * .021, floor + h * .03, math.max(2 * scale, span * .018), h * .046, c.accent)
  end
  stroke(spire, y + h * .12, spire + span * .075, y + h * .315, iceSnow, 1.2 * scale)
  stroke(spire + span * .074, y + h * .322, spire + span * .074, y + h * .88, c.accent, scale)
  for _, point in ipairs(snowSpecks) do
    ui.DrawCircleFilled(left + span * point[1], y + h * point[2], 1.25 * scale, .79, .90, .96, .50)
  end
end

local dragon = {.160, .132, .118, 1}
local dragonEdge = {.736, .515, .321, .50}

local function brokenAccord(x, y, w, h, scale, c)
  local base, left, span = y + h, x + w * .50, w * .50
  local crown = left + span * .58
  glow(crown, y + h * .32, h * .75, c.glow, .092)
  tri(left, base, left + span * .24, y + h * .58, left + span * .63, base, c.far)
  tri(left + span * .60, base, left + span * .90, y + h * .53, x + w, base, c.far)

  -- Wing membranes fold from their long leading edges toward the dragon's body.
  local bodyY = y + h * .40
  local leftTip, rightTip = left + span * .15, left + span * .91
  tri(crown - span * .035, bodyY, leftTip, y + h * .17, left + span * .29, y + h * .39, dragon)
  tri(crown - span * .035, bodyY, left + span * .29, y + h * .39, left + span * .39, y + h * .52, dragon)
  tri(crown, bodyY, rightTip, y + h * .12, left + span * .80, y + h * .40, dragon)
  tri(crown, bodyY, left + span * .80, y + h * .39, left + span * .73, y + h * .50, dragon)
  tri(crown - span * .07, bodyY, crown + span * .075, bodyY, crown + span * .02, y + h * .57, dragon)
  tri(crown - span * .02, bodyY, crown + span * .029, y + h * .27, crown + span * .055, bodyY, dragon)
  tri(crown + span * .011, y + h * .27, crown + span * .12, y + h * .27, crown + span * .041, y + h * .32, dragon)
  tri(crown + span * .026, y + h * .276, crown + span * .012, y + h * .208, crown + span * .052, y + h * .272, dragon)
  tri(crown + span * .020, y + h * .52, crown - span * .025, y + h * .64, crown - span * .122, y + h * .65, dragon)
  stroke(leftTip, y + h * .17, crown - span * .048, bodyY, dragonEdge, 1.2 * scale)
  stroke(crown, bodyY, rightTip, y + h * .12, dragonEdge, 1.2 * scale)

  -- The courts stand on a broad stepped base under the dragon's silhouette.
  tri(left + span * .30, y + h * .65, crown, y + h * .51, left + span * .87, y + h * .65, c.near)
  rect(left + span * .29, y + h * .647, span * .59, h * .048, c.near)
  for pillar = 0, 5 do
    local col = left + span * (.335 + pillar * .091)
    rect(col, y + h * .688, span * .041, h * .25, c.near)
    stroke(col + span * .036, y + h * .696, col + span * .036, y + h * .895, c.accent, scale)
  end
  for step = 0, 2 do
    rect(left + span * (.32 - step * .022), base - h * (.114 - step * .031),
      span * (.52 + step * .044), h * .039, c.near)
  end
  stroke(left + span * .30, y + h * .65, left + span * .87, y + h * .65, c.accent, scale)
end

local molten = {.982, .516, .230, .78}
local flare = {1, .756, .386, .68}
local embers = {{.21, .46}, {.37, .24}, {.42, .40}, {.55, .29}, {.67, .17}, {.81, .29}, {.92, .43}}

local function firefell(x, y, w, h, scale, c)
  local base, left, span = y + h, x + w * .49, w * .51
  glow(left + span * .61, y + h * .56, h * .89, c.glow, .088)
  tri(left, base, left + span * .31, y + h * .38, left + span * .68, base, c.far)
  tri(left + span * .62, base, left + span * .91, y + h * .26, x + w, base, c.far)
  tri(left + span * .11, base, left + span * .30, y + h * .65, left + span * .57, base, c.near)
  tri(left + span * .53, base, left + span * .92, y + h * .61, x + w, base, c.near)
  rect(left + span * .37, y + h * .68, span * .48, h * .22, c.near)
  tower(left + span * .40, base - h * .14, span * .10, h * .36, h * .13, c.near)
  tower(left + span * .54, base - h * .12, span * .16, h * .50, h * .16, c.near)
  tower(left + span * .74, base - h * .12, span * .09, h * .32, h * .09, c.near)
  rect(left + span * .59, y + h * .50, span * .023, h * .087, c.accent)
  rect(left + span * .626, y + h * .50, span * .023, h * .087, c.accent)
  stroke(left + span * .41, y + h * .51, left + span * .41, y + h * .72, c.accent, scale)
  stroke(left + span * .62, y + h * .22, left + span * .697, y + h * .38, c.accent, 1.35 * scale)

  local turn = left + span * .54
  tri(left + span * .62, y + h * .78, left + span * .68, y + h * .79, turn, y + h * .90, molten)
  tri(turn, y + h * .90, left + span * .56, y + h * .87, left + span * .78, base, molten)
  tri(turn, y + h * .90, left + span * .78, base, left + span * .61, base, molten)
  stroke(left + span * .65, y + h * .79, turn + scale, y + h * .895, flare, 1.4 * scale)
  stroke(turn + scale, y + h * .895, left + span * .687, base, flare, 2.1 * scale)
  for _, point in ipairs(embers) do
    ui.DrawCircleFilled(left + span * point[1], y + h * point[2], 1.3 * scale, 1, .63, .36, .60)
  end
end

local crystalFace = {.212, .418, .356, 1}
local crystalLight = {.659, .904, .754, .72}

local function crystal(x, base, w, h, scale, c)
  local tipX, tipY = x + w * .58, base - h
  local edgeY = base - h * .72
  local ridge = x + w * .58
  tri(x, edgeY, tipX, tipY, ridge, base, c.near)
  tri(x, edgeY, ridge, base, x, base, c.near)
  tri(tipX, tipY, x + w, edgeY, ridge, base, crystalFace)
  tri(x + w, edgeY, x + w, base, ridge, base, crystalFace)
  stroke(tipX, tipY, x + w, edgeY, crystalLight, 1.25 * scale)
  stroke(tipX, tipY, ridge, base, c.accent, scale)
end

local function sleepers(x, y, w, h, scale, c)
  local base, left, span = y + h, x + w * .49, w * .51
  local gate = left + span * .63
  glow(gate, y + h * .46, h * .69, c.glow, .11)
  tri(left, base, left + span * .29, y + h * .46, left + span * .54, base, c.far)
  tri(left + span * .65, base, left + span * .93, y + h * .38, x + w, base, c.far)
  tri(left + span * .11, y, left + span * .36, y, left + span * .26, y + h * .34, c.near)
  tri(left + span * .77, y, x + w, y, x + w, y + h * .52, c.near)

  tower(gate - span * .16, base - h * .075, span * .32, h * .50, h * .18, c.near)
  tower(gate - span * .083, base - h * .063, span * .166, h * .41, h * .11, c.far)
  tower(gate - span * .056, base - h * .04, span * .112, h * .38, h * .09, c.bottom)
  stroke(gate - span * .16, y + h * .425, gate, y + h * .245, c.accent, 1.4 * scale)
  stroke(gate, y + h * .245, gate + span * .16, y + h * .425, c.accent, 1.4 * scale)
  crystal(left + span * .31, base, span * .087, h * .31, scale, c)
  crystal(left + span * .76, base, span * .118, h * .55, scale, c)
  crystal(left + span * .87, base, span * .079, h * .33, scale, c)
  rect(left + span * .21, base - h * .055, span * .79, h * .055, c.near)
end

local relicFace = {.526, .375, .217, 1}

local function copium(x, y, w, h, scale, c)
  local cx, cy, radius = x + w * .79, y + h * .51, h * .37
  glow(cx, cy, radius * 2.3, c.glow, .10)
  ui.DrawCircleFilled(cx, cy, radius, c.near[1], c.near[2], c.near[3], c.near[4])
  ui.DrawCircle(cx, cy, radius, c.accent[1], c.accent[2], c.accent[3], .58, 1.4 * scale)
  ui.DrawCircle(cx, cy, radius * .76, c.accent[1], c.accent[2], c.accent[3], .36, scale)
  for marker = 0, 7 do
    local angle = math.pi * marker / 4
    local dx, dy = math.cos(angle), math.sin(angle)
    stroke(cx + dx * radius * .83, cy + dy * radius * .83,
      cx + dx * radius * .99, cy + dy * radius * .99, c.accent, scale)
  end
  local dw, dh = radius * .44, radius * .62
  tri(cx, cy - dh, cx - dw, cy, cx, cy + dh, c.far)
  tri(cx, cy - dh, cx + dw, cy, cx, cy + dh, relicFace)
  stroke(cx - dw, cy, cx, cy - dh, c.accent, 1.6 * scale)
  stroke(cx, cy - dh, cx + dw, cy, c.accent, 1.6 * scale)
end

local illustrations = {mistmoore = mistmoore, tofs = tofs, broken_accord = brokenAccord,
  firefell = firefell, sleepers = sleepers, copium = copium}

function M.palette(key)
  return themes[key] or themes.mistmoore
end

function M.draw(key, x, y, w, h, scale)
  local c = M.palette(key)
  if ui.DrawRectGradient then
    ui.DrawRectGradient(x, y, x + w, y + h,
      c.top[1], c.top[2], c.top[3], c.top[4], c.bottom[1], c.bottom[2], c.bottom[3], c.bottom[4])
  else
    rect(x, y, w, h, c.top)
  end
  if w >= 510 * scale then
    local illustrate = illustrations[key] or mistmoore
    illustrate(x, y, w, h, scale, c)
  elseif key ~= "mistmoore" then
    glow(x + w * .91, y + h * .32, math.min(w * .50, h * .69), c.glow, .035)
  end
end

return M
