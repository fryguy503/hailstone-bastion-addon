-- SPDX-License-Identifier: MIT
local M = {}
local pageSize = 12
local catalog, bySlug, zones, kinds
local detailFile, details

local function loadCatalog()
  if catalog then return end
  catalog = dofile("lib/encounter_library_index.lua") or {}
  bySlug, zones, kinds = {}, {}, {}
  local zoneNames, kindNames = {}, {}
  for _, entry in ipairs(catalog) do
    bySlug[entry.slug] = entry
    zoneNames[entry.zone] = true
    kindNames[entry.kind] = true
    entry.search = table.concat({entry.title, entry.zone, entry.group or "", entry.kind, entry.summary}, " "):lower()
  end
  for zone in pairs(zoneNames) do zones[#zones + 1] = zone end
  for kind in pairs(kindNames) do kinds[#kinds + 1] = kind end
  table.sort(zones)
  table.sort(kinds)
end

local function loadDetail(entry)
  if detailFile ~= entry.detailFile then
    details = dofile(entry.detailFile) or {}
    detailFile = entry.detailFile
  end
  return details and details[entry.slug] or {}
end

local function label(kind)
  return kind:sub(1, 1):upper() .. kind:sub(2)
end

local function metadata(entry)
  local parts = {entry.zone, label(entry.kind)}
  if entry.group and entry.group ~= "" and entry.group ~= entry.zone then parts[#parts + 1] = entry.group end
  if (entry.zoneVersion or 0) > 0 then parts[#parts + 1] = "Version " .. entry.zoneVersion end
  return table.concat(parts, " · ")
end

function M.open(view, slug)
  loadCatalog()
  local entry = bySlug[slug]
  if not entry then return end
  view.encounter = entry
  view.encounterBackY = view.pageScrollY
  view.scroll = true
end

function M.links(view, slugs, scale, style)
  loadCatalog()
  for _, slug in ipairs(slugs) do
    local entry = bySlug[slug]
    if entry then
      ui.Separator()
      if style.guideButton(entry.title, "featured:" .. slug, scale) then M.open(view, slug) end
      style.note(entry.summary, "muted")
    end
  end
end

local function selectFilter(id, current, all, values, name)
  local selected = current
  if ui.BeginCombo("##" .. id, current == "" and all or name(current)) then
    if ui.Selectable(all, current == "") then selected = "" end
    for _, value in ipairs(values) do
      if ui.Selectable(name(value) .. "##" .. id .. ":" .. value, current == value) then selected = value end
    end
    ui.EndCombo()
  end
  return selected
end

local function matches(library)
  if library.results then return library.results end
  local terms = {}
  for term in library.query:lower():gmatch("%S+") do terms[#terms + 1] = term end
  library.results = {}
  for _, entry in ipairs(catalog) do
    if (library.zone == "" or library.zone == entry.zone) and
        (library.kind == "" or library.kind == entry.kind) then
      local found = true
      for _, term in ipairs(terms) do
        if not entry.search:find(term, 1, true) then found = false; break end
      end
      if found then library.results[#library.results + 1] = entry end
    end
  end
  return library.results
end

function M.list(view, scale, style)
  loadCatalog()
  local library = view.library
  local width = ui.GetContentRegionAvail()
  style.featureHeading("Encounter Library", scale)
  style.note("Search published encounters by name, zone, or group.", "muted")
  ui.SetNextItemWidth(-1)
  local query = ui.InputTextWithHint("##journal-library-search", "Search encounters", library.query, 128)
  local inline = width >= 430 * scale
  ui.SetNextItemWidth(inline and (width - 8 * scale) / 2 or -1)
  local zone = selectFilter("journal-library-zone", library.zone, "All zones", zones, function(value) return value end)
  if inline then ui.SameLine() end
  ui.SetNextItemWidth(inline and (width - 8 * scale) / 2 or -1)
  local kind = selectFilter("journal-library-kind", library.kind, "All encounter types", kinds, label)
  if query ~= library.query or zone ~= library.zone or kind ~= library.kind then
    library.query, library.zone, library.kind = query, zone, kind
    library.page, library.results = 1, nil
  end

  local results = matches(library)
  if #results == 0 then
    style.note("No encounters match these filters.", "muted")
    return
  end
  local lastPage = math.ceil(#results / pageSize)
  if lastPage > 1 then
    ui.BeginDisabled(library.page == 1)
    if style.button("Previous##journal-library-prev", 0, 0) and library.page > 1 then
      library.page = library.page - 1
    end
    ui.EndDisabled()
    ui.SameLine()
    ui.BeginDisabled(library.page == lastPage)
    if style.button("Next##journal-library-next", 0, 0) and library.page < lastPage then
      library.page = library.page + 1
    end
    ui.EndDisabled()
  end
  local first = (library.page - 1) * pageSize + 1
  local last = math.min(#results, first + pageSize - 1)
  style.note(first .. "–" .. last .. " of " .. #results .. " encounters", "muted")
  for i = first, last do
    local entry = results[i]
    ui.Separator()
    if style.guideButton(entry.title, "library:" .. entry.slug, scale) then M.open(view, entry.slug) end
    style.note(metadata(entry), "gold")
    style.note(entry.summary, "muted")
  end
end

local function paragraphs(style, values, bullet)
  for _, value in ipairs(values or {}) do
    style.note((bullet and "• " or "") .. value)
  end
end

local function section(style, title, entries)
  if not entries or #entries == 0 then return false end
  ui.Separator()
  style.heading(title)
  return true
end

function M.detail(view, scale, style)
  local entry = view.encounter
  local back = view.mode == "library" and "Back to encounters" or "Back to expedition"
  if style.button(back .. "##journal-guide-back", 0, 0) then
    view.encounter = nil
    view.scroll = view.encounterBackY
    return
  end
  style.note(metadata(entry), "gold")
  style.featureHeading(entry.title, scale)
  style.note(entry.summary, "muted")
  local detail = loadDetail(entry)
  if section(style, "Overview", detail.overview) then paragraphs(style, detail.overview) end
  if section(style, "Role tips", detail.roles) then
    for _, role in ipairs(detail.roles) do
      style.note(role.label, "gold")
      paragraphs(style, role.tips, true)
    end
  end
  if section(style, "Phases", detail.phases) then
    for _, phase in ipairs(detail.phases) do
      style.note(phase.label, "gold")
      if phase.trigger and phase.trigger ~= "" then style.note(phase.trigger, "muted") end
      if phase.description and phase.description ~= "" then style.note(phase.description) end
    end
  end
  if section(style, "Abilities", detail.abilities) then
    for _, ability in ipairs(detail.abilities) do
      style.note(ability.name, "gold")
      if ability.summary and ability.summary ~= "" then style.note(ability.summary, "muted") end
      paragraphs(style, ability.description)
    end
  end
  for _, more in ipairs(detail.sections or {}) do
    ui.Separator()
    style.heading(more.title)
    paragraphs(style, more.paragraphs)
    paragraphs(style, more.bullets, true)
  end
end

return M
