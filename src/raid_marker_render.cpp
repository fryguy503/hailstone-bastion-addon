#include "raid_marker_render.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace bastion::raid_markers {
namespace {

using Color = std::array<float, 3>;
using Point = std::array<float, 3>;
using GlyphPoint = std::array<float, 2>;
struct Stroke { GlyphPoint from, to; };

// Match the silhouettes and colors on the toolbar. Normalize the strokes around their centers.
constexpr Stroke sunburst[] = {{{-1,0},{1,0}}, {{0,-1},{0,1}}, {{-.65f,-.65f},{.65f,.65f}}, {{-.65f,.65f},{.65f,-.65f}}};
constexpr Stroke cairn[] = {{{-1,-.65f},{0,1}}, {{0,1},{1,-.65f}}, {{1,-.65f},{-1,-.65f}}};
constexpr Stroke wave[] = {{{-1,-.5f},{-.5f,.55f}}, {{-.5f,.55f},{0,-.5f}}, {{0,-.5f},{.5f,.55f}}, {{.5f,.55f},{1,-.5f}}};
constexpr Stroke kite[] = {{{-1,0},{0,1}}, {{0,1},{1,0}}, {{1,0},{0,-1}}, {{0,-1},{-1,0}}};
constexpr Stroke fork[] = {{{-.85f,.8f},{0,0}}, {{.85f,.8f},{0,0}}, {{0,0},{0,-1}}};
constexpr Stroke grove[] = {{{-.8f,-.8f},{-.8f,.8f}}, {{0,-.8f},{0,1}}, {{.8f,-.8f},{.8f,.8f}}, {{-.8f,-.8f},{.8f,-.8f}}};
constexpr Stroke gate[] = {{{-.85f,-.8f},{-.85f,.35f}}, {{-.85f,.35f},{-.35f,.85f}}, {{-.35f,.85f},{.35f,.85f}},
                          {{.35f,.85f},{.85f,.35f}}, {{.85f,.35f},{.85f,-.8f}}};
constexpr Stroke orbit[] = {{{-.9f,-.4f},{-.9f,.4f}}, {{-.9f,.4f},{-.4f,.9f}}, {{-.4f,.9f},{.4f,.9f}},
                           {{.4f,.9f},{.9f,.4f}}, {{.9f,.4f},{.9f,-.4f}}, {{.9f,-.4f},{.4f,-.9f}},
                           {{.4f,-.9f},{-.4f,-.9f}}, {{-.4f,-.9f},{-.9f,-.4f}}};

struct Glyph { Color color; std::span<const Stroke> strokes; };
constexpr std::array<Glyph, 8> glyphs{{
    {{1,.72f,.25f}, sunburst}, {{1,.41f,.30f}, cairn}, {{.22f,.81f,1}, wave}, {{.80f,.49f,1}, kite},
    {{1,.44f,.74f}, fork}, {{.42f,.93f,.51f}, grove}, {{.41f,.60f,1}, gate}, {{.87f,.94f,.98f}, orbit},
}};

constexpr std::size_t ring_segments = 20;
constexpr std::uint64_t marker_tag = 0x5241494400000000ull;

void triangle(std::vector<HailstoneWorldVertex>& vertices, const Point& a, const Point& b, const Point& c,
              const Color& color, float alpha_a, float alpha_b, float alpha_c) {
    vertices.push_back({{a[0], a[1], a[2]}, {color[0], color[1], color[2], alpha_a}});
    vertices.push_back({{b[0], b[1], b[2]}, {color[0], color[1], color[2], alpha_b}});
    vertices.push_back({{c[0], c[1], c[2]}, {color[0], color[1], color[2], alpha_c}});
}

void quad(std::vector<HailstoneWorldVertex>& vertices, const Point& a, const Point& b, const Point& c,
          const Point& d, const Color& color, float bottom, float top) {
    triangle(vertices, a, b, c, color, bottom, bottom, top);
    triangle(vertices, a, c, d, color, bottom, top, top);
}

bool sample_floor(const PluginHostApi& host, const HailstoneWorldFrame& frame, const VisualMarker& marker,
                  Point& point, std::uint32_t& remaining_queries) {
    if (remaining_queries == 0) return false;
    --remaining_queries;
    constexpr float max_rise = 1.6f;
    constexpr float max_drop = 1.6f;
    float z = 0;
    if (host.world_floor_below(host.host, frame.frame, frame.epoch, point[0], point[1], marker.z + max_rise, &z) !=
            HAILSTONE_WORLD_RESOLVED || !std::isfinite(z) || z < marker.z - max_drop || z > marker.z + max_rise) return false;
    point[2] = z + .09f;
    return true;
}

void ring(std::vector<HailstoneWorldVertex>& vertices, const PluginHostApi& host, const HailstoneWorldFrame& frame,
          const VisualMarker& marker, const Color& color, bool preview, std::uint32_t& remaining_queries) {
    if (!(frame.capabilities & HAILSTONE_WORLD_TERRAIN) || !HAILSTONE_HOST_HAS(&host, world_floor_below) ||
        remaining_queries < 2 * ring_segments) return;
    struct Edge { Point inner, outer; bool ok; };
    std::array<Edge, ring_segments> edges{};
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const float angle = 2 * std::numbers::pi_v<float> * static_cast<float>(i) / ring_segments;
        const float x = std::cos(angle), y = std::sin(angle);
        auto& e = edges[i];
        e.inner = {marker.x + x * 1.43f, marker.y + y * 1.43f, 0};
        e.outer = {marker.x + x * 1.68f, marker.y + y * 1.68f, 0};
        e.ok = sample_floor(host, frame, marker, e.inner, remaining_queries) &&
            sample_floor(host, frame, marker, e.outer, remaining_queries);
    }
    const float alpha = preview ? .60f : .91f;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const auto& a = edges[i];
        const auto& b = edges[(i + 1) % edges.size()];
        if (!a.ok || !b.ok || (preview && i % 2 != 0)) continue;
        // Avoid drawing across drops rather than joining separate floor levels.
        const float lowest = std::min({a.inner[2], a.outer[2], b.inner[2], b.outer[2]});
        const float highest = std::max({a.inner[2], a.outer[2], b.inner[2], b.outer[2]});
        if (highest - lowest <= .7f) quad(vertices, a.inner, b.inner, b.outer, a.outer, color, alpha, alpha);
    }
}

void billboard(std::vector<HailstoneWorldVertex>& vertices, const HailstoneWorldFrame& frame,
               const VisualMarker& marker, const Glyph& glyph, bool preview) {
    const float dx = frame.camera_position[0] - marker.x;
    const float dy = frame.camera_position[1] - marker.y;
    const float length = std::hypot(dx, dy);
    const bool facing = std::isfinite(length) && length > .01f;
    const float right_x = facing ? dy / length : 1;
    const float right_y = facing ? -dx / length : 0;
    const auto at = [&](float right, float up) -> Point {
        return {marker.x + right * right_x, marker.y + right * right_y, marker.z + up};
    };
    const float alpha = preview ? .57f : .95f;
    const float height = preview ? 3.4f : 5.0f;
    if (!preview) {
        // Split into two gradient quads so the beam fades both vertically and at either side.
        triangle(vertices, at(-.85f,.14f), at(0,.14f), at(0,height-.7f), glyph.color, 0,.32f,.11f);
        triangle(vertices, at(-.85f,.14f), at(0,height-.7f), at(-.33f,height-.7f), glyph.color, 0,.11f,0);
        triangle(vertices, at(0,.14f), at(.85f,.14f), at(.33f,height-.7f), glyph.color, .32f,0,0);
        triangle(vertices, at(0,.14f), at(.33f,height-.7f), at(0,height-.7f), glyph.color, .32f,0,.11f);
    }
    for (const auto& s : glyph.strokes) {
        constexpr float size = .83f;
        constexpr float thickness = .13f;
        const float x0 = s.from[0] * size, z0 = s.from[1] * size + height;
        const float x1 = s.to[0] * size, z1 = s.to[1] * size + height;
        const float line = std::hypot(x1 - x0, z1 - z0);
        const float ox = thickness * (z0 - z1) / line, oz = thickness * (x1 - x0) / line;
        quad(vertices, at(x0+ox,z0+oz), at(x1+ox,z1+oz), at(x1-ox,z1-oz), at(x0-ox,z0-oz), glyph.color, alpha, alpha);
    }
}

bool valid(const VisualMarker& marker) {
    return marker.type > 0 && marker.type <= glyphs.size() &&
        std::isfinite(marker.x) && std::isfinite(marker.y) && std::isfinite(marker.z);
}

} // namespace

void draw_world(const PluginHostApi& host, const HailstoneWorldFrame& frame,
                std::span<const VisualMarker> placed, const std::optional<VisualMarker>& preview) {
    if (frame.size < sizeof(frame) || !(frame.capabilities & HAILSTONE_WORLD_GEOMETRY) || !frame.frame || !frame.epoch ||
        !HAILSTONE_HOST_HAS(&host, world_submit)) return;

    std::array<bool, glyphs.size()> drawn{};
    auto remaining_queries = frame.remaining_queries;
    auto remaining_vertices = frame.maximum_vertices;
    auto remaining_batches = frame.maximum_batches;
    std::vector<HailstoneWorldVertex> vertices;
    vertices.reserve(6 * ring_segments + 60);
    const auto submit = [&](const VisualMarker& marker, bool aiming) {
        if (!valid(marker) || !remaining_batches) return;
        const auto& glyph = glyphs[marker.type - 1];
        const auto billboard_vertices = 6 * glyph.strokes.size() + (aiming ? 0u : 12u);
        if (remaining_vertices < billboard_vertices) return;
        vertices.clear();
        billboard(vertices, frame, marker, glyph, aiming);
        const auto ring_vertices = 6 * (aiming ? ring_segments / 2 : ring_segments);
        if (remaining_vertices >= vertices.size() + ring_vertices)
            ring(vertices, host, frame, marker, glyph.color, aiming, remaining_queries);
        HailstoneWorldSubmission submission{};
        submission.size = sizeof(submission);
        submission.epoch = frame.epoch;
        submission.frame = frame.frame;
        submission.tag = marker_tag | (aiming ? 0x100u : 0u) | marker.type;
        submission.revision = frame.frame;
        submission.primitive = HAILSTONE_WORLD_TRIANGLES;
        submission.vertices = vertices.data();
        submission.vertex_count = vertices.size();
        if (host.world_submit(host.host, &submission)) {
            --remaining_batches;
            remaining_vertices -= static_cast<std::uint32_t>(vertices.size());
        }
    };
    if (preview) submit(*preview, true);
    for (const auto& marker : placed) {
        if (valid(marker) && !drawn[marker.type - 1]) {
            drawn[marker.type - 1] = true;
            submit(marker, false);
        }
    }
}

} // namespace bastion::raid_markers
