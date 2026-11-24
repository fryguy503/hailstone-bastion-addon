#pragma once

#include "hailstone_plugin.h"

#include <cstdint>
#include <optional>
#include <span>

namespace bastion::raid_markers {

struct VisualMarker {
    std::uint8_t type;
    float x, y, z; // Hailstone zone-file axes.
};

void draw_world(const PluginHostApi& host, const HailstoneWorldFrame& frame,
                std::span<const VisualMarker> placed, const std::optional<VisualMarker>& preview);

} // namespace bastion::raid_markers
