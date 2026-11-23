#include "telegraphs_geometry.h"
#include "../tools/telegraph_surface_fixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

namespace tel = bastion::telegraphs;
namespace {
std::size_t checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b, float tolerance = .001f) { return std::abs(a - b) < tolerance; }
struct Probe { float x, y, z; };
struct Queries final : tel::GeometryQueries {
    std::vector<Probe> probes;
    std::vector<std::uint32_t> anchors;
    std::map<std::uint32_t, tel::DrawnAnchor> drawn;
    std::vector<tel::fixture::Region> regions = {{tel::fixture::rectangle(-20000,20000,-20000,20000)}};
    std::vector<tel::fixture::Region> props;
    std::size_t patches=0;
    std::function<tel::SurfacePatchSample(const std::array<tel::TriangleVertex,3>&,std::size_t)> patch_override;
    std::function<tel::FloorSample(float, float, float)> sample = [](float, float, float probe) {
        return probe >= 0 ? tel::FloorSample{tel::SampleStatus::Resolved, 0} : tel::FloorSample{tel::SampleStatus::Missing, 0};
    };
    tel::FloorSample floor(float x, float y, float z) override { probes.push_back({x, y, z}); return sample(x, y, z); }
    tel::DrawnAnchor drawn_anchor(std::uint32_t id) override {
        anchors.push_back(id);
        const auto found = drawn.find(id);
        return found == drawn.end() ? tel::DrawnAnchor{tel::SampleStatus::Missing} : found->second;
    }
    tel::SurfacePatchSample surface_patch(const std::array<tel::TriangleVertex,3>& triangle,float rise) override {
        ++patches;
        if(patch_override) return patch_override(triangle,patches);
        return props.empty()?tel::fixture::certify(triangle,regions,rise):tel::fixture::certify_props(triangle,props,rise);
    }
};
tel::RenderMarker marker(tel::Shape shape = tel::Shape::Circle) {
    tel::RenderMarker m;
    m.packet.shape = shape; m.packet.id = 42; m.packet.generation = 7; m.packet.revision = 3;
    m.packet.flags = tel::Filled; m.packet.style = tel::Style::Solid;
    m.packet.server_now_ms = 100; m.packet.start_ms = 100; m.packet.impact_ms = 1100; m.packet.expire_ms = 1600;
    m.packet.p1 = 8; m.packet.p4 = 8; m.packet.color_argb = 0xc8123456u;
    if (shape == tel::Shape::Donut) m.packet.p2 = 4;
    if (shape == tel::Shape::Cone) m.packet.p2 = 90;
    if (shape == tel::Shape::Rectangle || shape == tel::Shape::Line || shape == tel::Shape::Cross) m.packet.p2 = 6;
    m.local_start = 100; m.local_impact = 1100; m.local_expire = 1600; m.tag = {11, 7, 42, 3, 9};
    return m;
}
tel::MarkerGeometry draw(const tel::RenderMarker& m, Queries& q, std::uint32_t now = 100, const tel::GeometryOptions& options = {}) {
    tel::GeometryBudget budget;
    return tel::build_geometry(m, now, q, budget, options);
}
float area(const tel::MarkerGeometry& batch) {
    float sum = 0;
    for (std::size_t i = 0; i < batch.vertices.size(); i += 3) {
        const auto a = batch.vertices[i], b = batch.vertices[i + 1], c = batch.vertices[i + 2];
        sum += .5f * std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
    }
    return sum;
}
bool finite(const tel::MarkerGeometry& batch) {
    return batch.vertices.size() % 3 == 0 && std::all_of(batch.vertices.begin(), batch.vertices.end(), [](const auto& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    });
}
bool alpha(const tel::MarkerGeometry& batch, unsigned value) {
    return std::all_of(batch.vertices.begin(), batch.vertices.end(), [value](const auto& v) { return v.argb >> 24 == value; });
}
bool has_color(const tel::MarkerGeometry& batch, std::uint32_t value) {
    return std::any_of(batch.vertices.begin(), batch.vertices.end(), [value](const auto& v) { return v.argb == value; });
}
bool covers_top(const tel::MarkerGeometry& batch, float x, float y, float z) {
    for (std::size_t n = 0; n < batch.vertices.size(); n += 3) {
        const auto a = batch.vertices[n], b = batch.vertices[n + 1], c = batch.vertices[n + 2];
        const double determinant = (static_cast<double>(b.y) - c.y) * (a.x - c.x) + (static_cast<double>(c.x) - b.x) * (a.y - c.y);
        if (std::abs(determinant) < 1e-12) continue;
        const double u = ((static_cast<double>(b.y) - c.y) * (x - c.x) + (static_cast<double>(c.x) - b.x) * (y - c.y)) / determinant;
        const double v = ((static_cast<double>(c.y) - a.y) * (x - c.x) + (static_cast<double>(a.x) - c.x) * (y - c.y)) / determinant;
        const float magnitude=std::max({std::abs(a.x),std::abs(a.y),std::abs(b.x),std::abs(b.y),std::abs(c.x),std::abs(c.y)});
        const double resolution=std::nextafter(magnitude,std::numeric_limits<float>::infinity())-magnitude;
        const double edge=std::max({std::hypot(a.x-b.x,a.y-b.y),std::hypot(b.x-c.x,b.y-c.y),std::hypot(c.x-a.x,c.y-a.y)});
        const double tolerance=std::max(1e-6,4*resolution*edge/std::abs(determinant));
        if (u >= -tolerance && v >= -tolerance && u + v <= 1 + tolerance && std::abs(u * a.z + v * b.z + (1 - u - v) * c.z - z) < .02) return true;
    }
    return false;
}
void atomic_failure(const tel::MarkerGeometry& batch, tel::GeometryStatus status, const char* message) {
    check(batch.status == status && batch.vertices.empty() && !batch.terrain_conformed && !batch.receipt().complete_geometry, message);
}

void silhouettes_and_coordinates() {
    Queries q;
    for (unsigned s = 1; s <= 11; ++s) {
        const auto batch = draw(marker(static_cast<tel::Shape>(s)), q);
        check(batch.complete() && !batch.vertices.empty() && finite(batch), "A supported shape failed CPU generation");
        check(batch.tag == marker().tag && batch.receipt().tag == batch.tag, "Geometry dropped its exact render provenance");
    }
    check(q.probes.empty() && q.anchors.empty(), "World unclamped geometry queried collision or actors");
    constexpr float pi = 3.14159265358979323846f;
    const float polygon_area = 4 * 64 * std::sin(pi / 4);
    auto m = marker();
    auto batch = draw(m, q);
    check(batch.vertices.size() == 24 && near(area(batch), polygon_area), "Circle footprint is not the eight-sided filled disk");
    check(near(batch.vertices[1].x, 0) && near(batch.vertices[1].y, 8), "Geometry swapped EQ X/Y or forward axes");
    check(near(batch.vertices[0].z, .035f) && !batch.terrain_conformed, "Unclamped Z policy changed");
    m.packet.heading = 128; m.packet.x = 100; m.packet.y = 200; m.packet.z = 30; m.packet.p5 = 2;
    batch = draw(m, q);
    check(near(batch.vertices[1].x, 108) && near(batch.vertices[1].y, 200) && near(batch.vertices[1].z, 32.035f), "512-unit heading/translation/offset was not applied");
    m.packet.heading = -384;
    check(near(draw(m, q).vertices[1].x, 108), "Negative equivalent heading was not normalized");
    m = marker(tel::Shape::Donut);
    check(near(area(draw(m, q)), polygon_area * .75f), "Donut did not retain its gameplay hole");
    m = marker(tel::Shape::Cone); m.packet.p3 = 2;
    check(near(area(draw(m, q)), 4 * 60 * std::sin(pi / 16)), "Cone angle/inner radius is not faithful");
    for (auto s : {tel::Shape::Rectangle, tel::Shape::Line, tel::Shape::Cross}) {
        m = marker(s); m.packet.p3 = 3;
        batch = draw(m, q);
        check(near(area(batch), s == tel::Shape::Cross ? 96.0f : 48.0f), "Rectangular/cross footprint dimensions changed");
        check(std::all_of(batch.vertices.begin(), batch.vertices.end(), [](const auto& v) { return v.y >= -1 && v.y <= 7; }), "Rectangle forward offset was ignored");
    }
    m = marker(); m.packet.flags = 0;
    check(draw(m, q).vertices.size() == 72, "Unset fill/outline flags did not default to both");
    m.packet.flags = tel::Outline;
    check(draw(m, q).vertices.size() == 48, "Outline-only marker unexpectedly filled its disk");
    m.packet.flags = tel::Filled; m.packet.p4 = 0;
    check(draw(m, q).vertices.size() == 72, "Automatic minimum 24 segments was not applied");
    m.packet.p1 = 100;
    check(draw(m, q).vertices.size() == 216, "Automatic segments did not cap at 72");
    m.packet.p4 = 128;
    check(draw(m, q).vertices.size() == 384, "Explicit 128-segment override was ignored");
}

void styles_and_palette() {
    Queries q;
    auto m = marker();
    for (unsigned s = 0; s <= 5; ++s) {
        m.packet.style = static_cast<tel::Style>(s);
        check(draw(m, q).complete(), "A supported style failed CPU generation");
    }
    m.packet.style = tel::Style::Striped;
    auto batch = draw(m, q);
    check(batch.vertices[1].argb >> 24 == 200 && batch.vertices[7].argb >> 24 == 48 && batch.vertices[13].argb >> 24 == 200,
          "Curved stripe pattern lost its two-sector cadence");
    m.packet.style = tel::Style::Checker; batch = draw(m, q);
    check(batch.vertices[1].argb >> 24 == 200 && batch.vertices[4].argb >> 24 == 60 &&
          batch.vertices[7].argb >> 24 == 60 && batch.vertices[10].argb >> 24 == 200, "Curved checker pattern changed");
    m.packet.style = tel::Style::Gradient; batch = draw(m, q);
    check(batch.vertices[0].argb >> 24 == 36 && batch.vertices[1].argb >> 24 == 84 && batch.vertices[4].argb >> 24 == 190,
          "Gradient center or alternating outer-sector alpha changed");
    m.packet.style = tel::Style::Ring; batch = draw(m, q);
    check(batch.vertices.size() == 72 && batch.vertices[0].argb >> 24 == 36 && near(batch.vertices[0].x, 0) && near(batch.vertices[0].y, 0),
          "Decorative Ring cut a hole out of the hazard disk");
    m.packet.flags = tel::Outline; batch = draw(m, q);
    check(batch.vertices.size() == 96, "Ring accent did not survive outline-only flags");
    m = marker(tel::Shape::Stack); m.packet.style = tel::Style::Auto;
    check(draw(m, q).vertices.size() == 72, "Stack auto style did not select decorative Ring");
    m = marker(tel::Shape::Rectangle); m.packet.style = tel::Style::Gradient;
    batch = draw(m, q);
    check(batch.vertices[0].argb >> 24 == 70 && batch.vertices[1].argb >> 24 == 200, "Quad gradient is not lateral");
    m.packet.style = tel::Style::Striped;
    check(draw(m, q).vertices.size() == 12, "Quad stripe overlay bands were omitted");
    m.packet.style = tel::Style::Checker;
    check(draw(m, q).vertices.size() == 12, "Quad checker must share stripe overlay behavior");
    m = marker(); m.packet.color_argb = 0;
    const std::array<std::uint32_t, 5> palette{0xa8ff4338u, 0xa8ff4338u, 0x9848b5ffu, 0xa8ffcc33u, 0x9840e69du};
    for (unsigned d = 0; d <= 4; ++d) {
        m.packet.disposition = static_cast<tel::Disposition>(d);
        check(draw(m, q).vertices[0].argb == palette[d], "Disposition palette precedence changed");
    }
    m.packet.disposition = tel::Disposition::Auto; m.packet.mechanic = tel::Mechanic::Gaze;
    check(draw(m, q).vertices[0].argb == 0xa8e65cffu, "Auto disposition ignored mechanic palette");
    m.packet.color_argb = 0xc8123456u; m.packet.disposition = tel::Disposition::Safe;
    check(draw(m, q).vertices[0].argb == 0xc8123456u, "Explicit color did not override palette");
    tel::GeometryOptions opts; opts.opacity_percent = 25;
    check(alpha(draw(m, q, 100, opts), 50), "Opacity setting did not scale alpha");
    m.packet.flags |= tel::Pulse;
    check(alpha(draw(m, q, 675), 152) && alpha(draw(m, q, 225), 200), "Pulse period or extrema changed");
    m.packet.flags |= tel::RequiredDisplay; m.packet.style = tel::Style::Gradient;
    opts.opacity_percent = 0; batch = draw(m, q, 675, opts);
    check(std::all_of(batch.vertices.begin(), batch.vertices.end(), [](const auto& v) { return (v.argb >> 24) >= 128; }),
          "Required warning was dimmed by opacity, pulse or pattern");
}

void glyphs_and_countdown() {
    Queries q;
    auto m = marker();
    for (unsigned mechanic = 0; mechanic <= 10; ++mechanic) {
        m.packet.mechanic = static_cast<tel::Mechanic>(mechanic); m.packet.flags = tel::Filled | tel::Directional;
        const auto batch = draw(m, q);
        check(batch.complete() && finite(batch), "A supported mechanic failed geometry generation");
    }
    m = marker(tel::Shape::Stack); m.packet.flags |= tel::Directional;
    auto batch = draw(m, q);
    check(batch.vertices.size() == 96 && has_color(batch, 0xffffffffu) && has_color(batch, 0xff050505u), "Stack did not emit four contrasting inward arrows");
    check(std::any_of(batch.vertices.begin(), batch.vertices.end(), [](const auto& v) { return std::abs(v.x) > 8 || std::abs(v.y) > 8; }),
          "Radial glyph was clipped to canonical hazard footprint");
    m = marker(tel::Shape::Spread); m.packet.flags |= tel::Directional;
    check(draw(m, q).vertices.size() == 216, "Spread pips are not four halo/core octagons");
    m.packet.mechanic = tel::Mechanic::Stack;
    check(draw(m, q).vertices.size() == 216, "Mechanic incorrectly replaced shape's radial glyph");
    m = marker(); m.packet.mechanic = tel::Mechanic::Stack; m.packet.flags |= tel::Directional;
    check(draw(m, q).vertices.size() == 114, "Mechanic radial and generic directional arrows must coexist");
    m.packet.flags = tel::Filled;
    check(draw(m, q).vertices.size() == 24, "Directional glyph ignored its flag");
    m.packet.mechanic = tel::Mechanic::Soak;
    check(draw(m, q).vertices.size() == 72, "Soak accent ring incorrectly requires Directional");
    m.packet.mechanic = tel::Mechanic::MoveAway;
    check(draw(m, q).vertices.size() == 72, "MoveAway accent ring incorrectly requires Directional");
    m.packet.mechanic = tel::Mechanic::Split;
    check(draw(m, q).vertices.size() == 36, "Split crossbar was omitted");
    m.packet.mechanic = tel::Mechanic::Interrupt;
    check(draw(m, q).vertices.size() == 36, "Interrupt X was omitted");
    m = marker(tel::Shape::Target);
    check(draw(m, q).vertices.size() == 36, "Target crosshair was omitted");
    m = marker(tel::Shape::Gaze); m.packet.flags |= tel::Directional;
    check(draw(m, q).vertices.size() == 42, "Gaze arrow duplicated generic directional arrow");
    m = marker(); m.packet.flags |= tel::Countdown;
    check(draw(m, q, 100).vertices.size() == 72 && draw(m, q, 600).vertices.size() == 48 && draw(m, q, 1099).vertices.size() == 30,
          "Countdown arc does not shrink using remaining/total segments");
    batch = draw(m, q, 600);
    check(batch.vertices[24].argb == 0xff123456u, "Countdown is not opaque palette RGB");
    check(draw(m, q, 1100).vertices.size() == 24, "Countdown persisted beyond impact");
    atomic_failure(draw(m, q, 99), tel::GeometryStatus::NotStarted, "Marker drew before start");
    atomic_failure(draw(m, q, 1600), tel::GeometryStatus::Expired, "Marker drew at expiry");
    m.local_start = 0xffffff00u; m.local_impact = m.local_start + 1000; m.local_expire = m.local_start + 1500;
    check(draw(m, q, m.local_start + 500).vertices.size() == 48, "Countdown broke across uint32 clock wrap");
    atomic_failure(draw(m, q, m.local_expire), tel::GeometryStatus::Expired, "Expiry broke across uint32 clock wrap");
}

void anchors() {
    Queries q;
    q.drawn[71] = {tel::SampleStatus::Resolved, 100, 200, 30, 128};
    q.drawn[72] = {tel::SampleStatus::Resolved, 300, 400, 50, 256};
    auto m = marker(); m.packet.owner_id = 71; m.packet.anchor_id = 72; m.packet.anchor = tel::Anchor::Owner;
    m.packet.flags |= tel::FollowPosition;
    auto batch = draw(m, q);
    check(q.anchors.back() == 71 && near(batch.vertices[1].x, 100) && near(batch.vertices[1].y, 208) && near(batch.vertices[1].z, 30.035f),
          "FollowPosition also changed heading or ignored actual drawn anchor");
    m.packet.flags = tel::Filled | tel::FollowHeading; batch = draw(m, q);
    check(near(batch.vertices[1].x, 8) && near(batch.vertices[1].y, 0) && near(batch.vertices[1].z, .035f), "FollowHeading also changed position");
    for (auto a : {tel::Anchor::Entity, tel::Anchor::Target}) {
        m.packet.anchor = a; m.packet.flags |= tel::FollowPosition; batch = draw(m, q);
        check(q.anchors.back() == 72 && near(batch.vertices[1].x, 300) && near(batch.vertices[1].y, 392), "Entity/Target used owner or wrong axes");
    }
    m.packet.anchor_id = 99;
    atomic_failure(draw(m, q), tel::GeometryStatus::MissingAnchor, "Missing anchor silently fell back to packet position");
    q.drawn[99] = {tel::SampleStatus::Deferred};
    atomic_failure(draw(m, q), tel::GeometryStatus::DeferredAnchor, "Unavailable drawn pose did not defer entire marker");
    q.drawn[99] = {tel::SampleStatus::Resolved, 0, 0, 0, std::numeric_limits<float>::quiet_NaN()};
    atomic_failure(draw(m, q), tel::GeometryStatus::Numerical, "Invalid drawn heading was accepted");
    m.packet.flags = tel::Filled;
    check(draw(m, q).complete(), "Anchored marker without follow flags unnecessarily required live actor");
    m.packet.anchor = tel::Anchor::World; m.packet.flags |= tel::FollowPosition | tel::FollowHeading;
    const auto before = q.anchors.size();
    check(draw(m, q).complete() && q.anchors.size() == before, "World anchor followed a logical actor");
    // Drawn origin and encounter floor can differ substantially (tall actors,
    // hovering models). A validated floor cache prevents bridge layer capture.
    m = marker(); m.packet.anchor = tel::Anchor::Owner; m.packet.owner_id = 71;
    m.packet.flags |= tel::FollowPosition | tel::ClampToTerrain | tel::RequiredDisplay;
    q.drawn[71] = {tel::SampleStatus::Resolved, 100, 200, 30, 0, true, 0};
    q.sample = [](float, float, float probe) {
        if (probe >= 40) return tel::FloorSample{tel::SampleStatus::Resolved, 40};
        if (probe >= 0) return tel::FloorSample{tel::SampleStatus::Resolved, 0};
        return tel::FloorSample{tel::SampleStatus::Missing};
    };
    batch = draw(m, q);
    check(batch.complete() && batch.terrain_conformed && near(batch.vertices[0].z, .035f), "Following actor selected overhead bridge instead of validated ground reference");
    q.drawn[71].floor_reference_valid = false;
    batch = draw(m, q);
    check(batch.complete() && near(batch.vertices[0].z, 40.035f), "Missing ground cache did not retain actual drawn Z as native layer reference");
    q.drawn[71].floor_reference_valid = true; q.drawn[71].floor_z = std::numeric_limits<float>::quiet_NaN();
    atomic_failure(draw(m, q), tel::GeometryStatus::Numerical, "Flagged non-finite anchor floor cache was trusted");
    m.packet.flags &= static_cast<std::uint16_t>(~tel::ClampToTerrain);
    batch = draw(m, q);
    check(batch.complete() && near(batch.vertices[0].z, 30.035f), "Unclamped actor geometry used cached floor instead of actual drawn Z");
}

void following_terrain_refines_unavailable_patches() {
    Queries q;
    auto m = marker();
    m.packet.anchor = tel::Anchor::Owner;
    m.packet.owner_id = 71;
    m.packet.flags |= tel::FollowPosition | tel::ClampToTerrain | tel::RequiredDisplay;
    q.drawn[71] = {tel::SampleStatus::Resolved, 100, 200, 0, 0};
    tel::GeometryOptions options;
    options.bounded_draping = true;
    q.patch_override = [](const auto& points, std::size_t) {
        const auto& a = points[0];
        const auto& b = points[1];
        const auto& c = points[2];
        if (std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) > 24)
            return tel::SurfacePatchSample{};
        return tel::SurfacePatchSample{tel::PatchStatus::Planar};
    };
    const auto refined = draw(m, q, 600, options);
    check(refined.complete() && refined.terrain_conformed && refined.vertices.size() > 24 &&
          near(area(refined), 4 * 64 * std::sin(3.14159265358979323846f / 4), .01f),
          "Refining the actor's terrain proof lost coverage of the warning footprint");
    q.patch_override = [](const auto&, std::size_t) { return tel::SurfacePatchSample{}; };
    atomic_failure(draw(m, q, 600, options), tel::GeometryStatus::DeferredFloor,
                   "Uncertified actor terrain emitted a complete ground warning");
}

void terrain() {
    Queries q;
    auto m = marker(tel::Shape::Rectangle); m.packet.p1 = 40; m.packet.p2 = 24;
    m.packet.flags |= tel::ClampToTerrain | tel::RequiredDisplay; m.packet.z = 5; m.packet.p5 = .5f;
    q.sample = [](float x, float y, float probe) {
        const float z = 5 + .3f * x + .7f * y;
        return tel::FloorSample{probe >= z ? tel::SampleStatus::Resolved : tel::SampleStatus::Missing, z};
    };
    auto batch = draw(m, q);
    check(batch.complete() && batch.terrain_conformed && batch.receipt().terrain_conformed && batch.vertices.size() > 6,
          "Sloped terrain did not adaptively subdivide");
    check(std::all_of(batch.vertices.begin(), batch.vertices.end(), [](const auto& v) { return near(v.z, 5 + .3f * v.x + .7f * v.y + .535f); }),
          "Terrain Z was interpolated instead of sampled per vertex");
    check(std::all_of(q.probes.begin(), q.probes.end(), [](const auto& p) { return p.z <= 101.001f; }), "Terrain probe did not start at reference+96");
    q.probes.clear(); m = marker(); m.packet.flags |= tel::ClampToTerrain | tel::RequiredDisplay; m.packet.z = 10;
    q.sample = [](float, float, float probe) {
        for (float z : {70.0f, 12.0f, 9.0f, -20.0f}) if (z <= probe) return tel::FloorSample{tel::SampleStatus::Resolved, z};
        return tel::FloorSample{tel::SampleStatus::Missing};
    };
    batch = draw(m, q);
    check(batch.complete() && near(batch.vertices[0].z, 9.035f), "Overhead bridge hid nearer reference floor");
    check(q.probes.size() >= 3 && near(q.probes[0].z, 106) && near(q.probes[1].z, 68) && near(q.probes[2].z, 10),
          "Layer descent did not use reference+96 and candidate-2");
    // Exactly eight descending layers are allowed; no unbounded floor walk.
    q.sample = [](float, float, float probe) {
        for (float z : {80.0f, 70.0f, 60.0f, 50.0f, 40.0f, 30.0f, 20.0f, 10.0f, 0.0f})
            if (z <= probe) return tel::FloorSample{tel::SampleStatus::Resolved, z};
        return tel::FloorSample{tel::SampleStatus::Missing};
    };
    m.packet.z = 0; q.probes.clear(); batch = draw(m, q);
    check(batch.complete() && near(batch.vertices[0].z, 10.035f), "Layer walk did not cap at eight candidates");
    check(q.probes.size() % 8 == 0, "Eight-layer probe cap was exceeded");
    float lift = 2;
    q.sample = [&lift](float, float, float) { return tel::FloorSample{tel::SampleStatus::Resolved, lift}; };
    m.packet.z = 5; const auto old = draw(m, q); lift = 3; const auto moved = draw(m, q);
    check(old.complete() && moved.complete() && near(moved.vertices[0].z - old.vertices[0].z, 1), "Moving lift reused stale prior-frame terrain");
    q.sample = [](float, float, float) { return tel::FloorSample{tel::SampleStatus::Resolved, -128}; };
    m.packet.z = 0;
    check(draw(m, q).complete(), "Floor exactly 128 from reference was rejected");
    q.sample = [](float, float, float) { return tel::FloorSample{tel::SampleStatus::Resolved, -128.1f}; };
    atomic_failure(draw(m, q), tel::GeometryStatus::MissingFloor, "Far-away floor silently replaced missing encounter surface");
    q.sample = [](float, float, float) { return tel::FloorSample{tel::SampleStatus::Missing}; };
    atomic_failure(draw(m, q), tel::GeometryStatus::MissingFloor, "Missing floor produced a flat fallback");
    q.sample = [](float, float, float) { return tel::FloorSample{tel::SampleStatus::Deferred}; };
    atomic_failure(draw(m, q), tel::GeometryStatus::DeferredFloor, "Deferred collision produced a partial marker");
    q.sample = [](float x, float, float) { return x > 1 ? tel::FloorSample{tel::SampleStatus::Missing} : tel::FloorSample{tel::SampleStatus::Resolved, 0}; };
    atomic_failure(draw(m, q), tel::GeometryStatus::MissingFloor, "Late missing vertex did not roll back whole marker");
    m.packet.flags &= static_cast<std::uint16_t>(~tel::RequiredDisplay);
    atomic_failure(draw(m, q), tel::GeometryStatus::MissingFloor, "Optional marker used unsafe flat fallback");
    m.packet.flags |= tel::RequiredDisplay;
    q.sample = [](float x, float, float probe) {
        const float z = x >= .37f ? 10.0f : 0.0f;
        return tel::FloorSample{probe >= z ? tel::SampleStatus::Resolved : tel::SampleStatus::Missing, z};
    };
    atomic_failure(draw(m, q), tel::GeometryStatus::Cliff, "Discontinuous floor was bridged by a stretched triangle");
    q.sample = [](float, float, float probe) { return tel::FloorSample{tel::SampleStatus::Resolved, probe + 1}; };
    atomic_failure(draw(m, q), tel::GeometryStatus::Numerical, "Floor above requested probe was trusted");
    q.sample = [](float, float, float) { return tel::FloorSample{tel::SampleStatus::Resolved, std::numeric_limits<float>::quiet_NaN()}; };
    atomic_failure(draw(m, q), tel::GeometryStatus::Numerical, "NaN floor was accepted");
    // Terrain conformity applies to ribbons and mechanic glyphs as well as fill.
    q.sample = [](float x, float y, float probe) {
        const float z = .1f * x + .2f * y;
        return tel::FloorSample{probe >= z ? tel::SampleStatus::Resolved : tel::SampleStatus::Missing, z};
    };
    m.packet.flags |= tel::Outline | tel::Directional | tel::Countdown; m.packet.mechanic = tel::Mechanic::Stack;
    batch = draw(m, q);
    check(batch.complete() && std::all_of(batch.vertices.begin(), batch.vertices.end(), [](const auto& v) { return near(v.z, .1f * v.x + .2f * v.y + .035f); }),
          "Outline, countdown or directional glyph floated off terrain");
}

void budgets_and_invalid_input() {
    Queries q;
    auto m = marker();
    tel::GeometryBudget budget; budget.vertex_limit = 23;
    atomic_failure(tel::build_geometry(m, 100, q, budget), tel::GeometryStatus::Budget, "Vertex cap returned partial disk");
    check(budget.vertices_used == 0, "Failed marker consumed committed vertex capacity");
    budget = {}; budget.vertex_limit = 48;
    check(tel::build_geometry(m, 100, q, budget).complete() && tel::build_geometry(m, 100, q, budget).complete() && budget.vertices_used == 48,
          "Frame did not share vertex budget between markers");
    atomic_failure(tel::build_geometry(m, 100, q, budget), tel::GeometryStatus::Budget, "Exhausted shared vertex budget was ignored");
    budget = {}; budget.work_limit = 0;
    atomic_failure(tel::build_geometry(m, 100, q, budget), tel::GeometryStatus::Budget, "Zero work budget was ignored");
    budget = {}; budget.vertex_limit = 24; budget.required_markers_remaining = 1;
    atomic_failure(tel::build_geometry(m, 100, q, budget), tel::GeometryStatus::Budget, "Optional marker consumed reserved required geometry");
    m.packet.flags |= tel::RequiredDisplay;
    check(tel::build_geometry(m, 100, q, budget).complete(), "Required marker could not consume its vertex reserve");
    m.packet.flags |= tel::ClampToTerrain; budget = {}; budget.floor_query_limit = 1;
    atomic_failure(tel::build_geometry(m, 100, q, budget), tel::GeometryStatus::Budget, "Terrain budget exhausted after first vertex returned partial marker");
    check(budget.floor_queries_used == 1 && budget.vertices_used == 0, "Failed terrain work was refunded or partial vertices committed");
    m.packet.flags &= static_cast<std::uint16_t>(~tel::RequiredDisplay);
    budget = {}; budget.required_markers_remaining = 1; budget.floor_query_limit = 1536; budget.floor_queries_used = 512;
    atomic_failure(tel::build_geometry(m, 100, q, budget), tel::GeometryStatus::Budget, "Optional marker consumed required floor reserve");
    m.packet.flags |= tel::RequiredDisplay;
    check(tel::build_geometry(m, 100, q, budget).complete(), "Required marker could not consume reserved terrain probes");
    m = marker();
    tel::GeometryOptions options; options.ribbon_width = 0;
    atomic_failure(draw(m, q, 100, options), tel::GeometryStatus::Invalid, "Zero ribbon width was accepted");
    options.ribbon_width = 9;
    atomic_failure(draw(m, q, 100, options), tel::GeometryStatus::Invalid, "Unbounded ribbon width was accepted");
    options = {}; options.opacity_percent = 101;
    atomic_failure(draw(m, q, 100, options), tel::GeometryStatus::Invalid, "Unbounded opacity was accepted");
    m.packet.flags = 0x8000;
    atomic_failure(draw(m, q), tel::GeometryStatus::Invalid, "Unknown render flags produced geometry");
    m = marker(); m.packet.p1 = std::numeric_limits<float>::infinity();
    atomic_failure(draw(m, q), tel::GeometryStatus::Invalid, "Non-finite shape dimension was accepted");
    m = marker(); m.packet.shape = tel::Shape::None;
    atomic_failure(draw(m, q), tel::GeometryStatus::Invalid, "Unsupported shape could claim completion");
    m = marker(); m.local_impact = m.local_start;
    atomic_failure(draw(m, q), tel::GeometryStatus::Invalid, "Invalid local timing produced geometry");
    m = marker(); ++m.tag.revision;
    atomic_failure(draw(m, q), tel::GeometryStatus::Invalid, "Geometry could certify a different wire revision");
    m = marker(); ++m.tag.id;
    atomic_failure(draw(m, q), tel::GeometryStatus::Invalid, "Geometry could certify a different marker ID");
    m = marker(); m.tag.incarnation = 0;
    atomic_failure(draw(m, q), tel::GeometryStatus::Invalid, "Geometry without local provenance could certify completion");
    m = marker(); m.local_expire += 1;
    atomic_failure(draw(m, q), tel::GeometryStatus::Invalid, "Geometry accepted local timing inconsistent with its wire lifetime");
    m = marker(tel::Shape::Rectangle); m.packet.flags = tel::Outline; m.packet.x = m.packet.y = 1000000;
    options = {}; options.ribbon_width = .01f;
    atomic_failure(draw(m, q, 100, options), tel::GeometryStatus::Numerical, "Rounded zero-area ribbons could certify geometry completion");
    m.packet.flags |= tel::ClampToTerrain | tel::RequiredDisplay;
    atomic_failure(draw(m, q, 100, options), tel::GeometryStatus::Numerical, "Terrain receipt certified collapsed ribbon triangles");
    m = marker(); m.packet.x = m.packet.y = 1000000; m.packet.p1 = .001f;
    atomic_failure(draw(m, q), tel::GeometryStatus::Numerical, "Rounded zero-area hazard footprint could certify completion");
    // All protocol-valid worst-case dimensions remain bounded, even when the
    // shared floor/work budgets prevent a full required marker this frame.
    m = marker(tel::Shape::Cross); m.packet.flags |= tel::ClampToTerrain | tel::RequiredDisplay | tel::Outline | tel::Directional | tel::Countdown;
    m.packet.p1 = 10000; m.packet.p2 = 10000; m.packet.p3 = 10000; m.packet.p4 = 128;
    budget = {}; const auto batch = tel::build_geometry(m, 100, q, budget);
    check((batch.complete() || batch.status == tel::GeometryStatus::Budget || batch.status == tel::GeometryStatus::Cliff) &&
          budget.floor_queries_used <= 262144 && budget.work_used <= 1048576 && batch.vertices.size() <= 393216,
          "Maximum dimensions escaped bounded CPU resources");
}

void complete_supported_matrix() {
    Queries q;
    // Exercise each shape/style with every supported flag, including curved
    // gradients, ring outlines, countdown and all geometry on a sampled floor.
    for (unsigned shape = 1; shape <= 11; ++shape) {
        for (unsigned style = 0; style <= 5; ++style) {
            auto m = marker(static_cast<tel::Shape>(shape));
            m.packet.style = static_cast<tel::Style>(style); m.packet.flags = tel::kKnownFlags;
            tel::GeometryBudget budget;
            const auto batch = tel::build_geometry(m, 600, q, budget);
            check(batch.complete() && batch.terrain_conformed && finite(batch) &&
                  std::all_of(batch.vertices.begin(), batch.vertices.end(), [](const auto& v) { return (v.argb >> 24) >= 128 && near(v.z, .035f); }),
                  "A shape/style/all-flags combination did not produce complete, readable, conforming geometry");
            check(budget.vertices_used == batch.vertices.size() && budget.floor_queries_used <= 1536 && budget.work_used <= 1048576,
                  "Shape/style matrix bypassed shared frame accounting");
        }
    }
    // An earlier valid layer cannot turn later unavailable collision into a
    // complete result. This matters while collision streams or lifts change.
    auto m = marker(); m.packet.flags |= tel::ClampToTerrain | tel::RequiredDisplay;
    q.sample = [](float, float, float probe) {
        return probe >= 20 ? tel::FloorSample{tel::SampleStatus::Resolved, 20} : tel::FloorSample{tel::SampleStatus::Deferred};
    };
    atomic_failure(draw(m, q), tel::GeometryStatus::DeferredFloor, "Partial layered search could certify unavailable terrain");
    q.sample = [](float, float, float) { return tel::FloorSample{static_cast<tel::SampleStatus>(99), 0}; };
    atomic_failure(draw(m, q), tel::GeometryStatus::DeferredFloor, "Unknown query status was accepted as a resolved surface");
}
} // namespace

namespace {
void enhanced_presentation() {
    Queries q;
    auto m = marker();
    m.packet.disposition = tel::Disposition::Safe;
    tel::GeometryOptions options; options.unified_palette = true;
    check(draw(m, q, 100, options).vertices.front().argb == options.safe, "unified palette ignored explicit safe meaning");
    options.unified_palette = false;
    check(draw(m, q, 100, options).vertices.front().argb == m.packet.color_argb, "server color option lost authored RGB");
    options.enhanced_style = true;
    m.packet.flags |= tel::Outline | tel::Countdown;
    const auto batch = draw(m, q, 600, options);
    check(batch.complete() && has_color(batch, 0x90131d2au) && has_color(batch, 0xffedf4fbu) && batch.vertices.size() > draw(m, q, 600).vertices.size(),
          "contrast edge/countdown track missing");
    options.unified_palette = true; options.opacity_percent = 0;
    for (unsigned shape = 1; shape <= 11; ++shape) {
        for (unsigned style = 0; style <= 5; ++style) {
            m = marker(static_cast<tel::Shape>(shape)); m.packet.style = static_cast<tel::Style>(style);
            m.packet.flags = tel::kKnownFlags;
            tel::GeometryBudget budget;
            const auto result = tel::build_geometry(m, 900, q, budget, options);
            check(result.complete() && result.terrain_conformed && finite(result) &&
                  std::all_of(result.vertices.begin(), result.vertices.end(), [](const auto &v) { return (v.argb >> 24) >= 128; }),
                  "enhanced shape/style lost complete required warning at zero optional opacity");
            check(budget.vertices_used == result.vertices.size() && budget.floor_queries_used <= tel::GeometryBudget::kMaximumFloorQueries,
                  "enhanced presentation escaped frame budgets");
        }
    }
}
void bounded_draping() {
    Queries q;
    tel::GeometryOptions options; options.enhanced_style = options.bounded_draping = true;
    auto m = marker(tel::Shape::Rectangle);
    m.packet.flags = tel::Filled | tel::Outline | tel::ClampToTerrain | tel::RequiredDisplay | tel::Countdown | tel::Directional;
    m.packet.p1 = 12; m.packet.p2 = 36; m.packet.x = -4;
    const auto stair_top = [](float x) { return x < 0 ? 0.0f : std::min(16.0f, (std::floor(x / 3) + 1) * 4); };
    q.regions={{tel::fixture::rectangle(-20000,0,-20000,20000)}};
    for(unsigned n=0;n<4;++n) q.regions.push_back({tel::fixture::rectangle(n*3,n==3?20000:(n+1)*3,-20000,20000),0,0,static_cast<double>((n+1)*4)});
    q.sample = [stair_top](float x, float, float probe) {
        // Ground remains below all stair treads; the upper landing also has an
        // unrelated roof. A fixed anchor+6 cap loses the 8/12/16 upper treads.
        for (float z : {40.0f, stair_top(x), 0.0f}) if (z <= probe) return tel::FloorSample{tel::SampleStatus::Resolved, z};
        return tel::FloorSample{tel::SampleStatus::Missing};
    };
    auto batch = draw(m, q, 600, options);
    check(batch.complete() && batch.terrain_conformed, "connected multi-step staircase did not complete");
    const auto stair_vertex=[stair_top](const auto& v) {
        if(near(v.z,stair_top(v.x)+.035f)) return true;
        // A vertical edge has two legitimate one-sided floor heights. Exact
        // native face clipping duplicates that edge without an artificial ramp.
        for(float boundary:{0.0f,3.0f,6.0f,9.0f})
            if(near(v.x,boundary,.0001f)&&(near(v.z,stair_top(boundary-.001f)+.035f)||near(v.z,stair_top(boundary+.001f)+.035f))) return true;
        return false;
    };
    check(std::all_of(batch.vertices.begin(), batch.vertices.end(), stair_vertex),
          "staircase snapped to underlying ground or overhead landing");
    bool crossed_riser = false;
    for (std::size_t n = 0; n < batch.vertices.size(); n += 3) {
        const auto& a = batch.vertices[n]; const auto& b = batch.vertices[n + 1]; const auto& c = batch.vertices[n + 2];
        const float expected = stair_top((a.x + b.x + c.x) / 3) + .035f;
        if (!near((a.z + b.z + c.z) / 3, expected, .01f)) crossed_riser = true;
    }
    check(!crossed_riser, "triangles interpolated through a stair riser and buried the upper tread");
    for (float expected : {0.0f, 4.0f, 8.0f, 12.0f, 16.0f})
        check(std::any_of(batch.vertices.begin(), batch.vertices.end(), [expected](const auto& v) { return near(v.z, expected + .035f); }),
              "staircase omitted an intermediate tread");
    // Repeat from the upper landing to prove descending continuity too.
    m.packet.x = 14; m.packet.z = 16;
    batch = draw(m, q, 600, options);
    check(batch.complete() && std::all_of(batch.vertices.begin(), batch.vertices.end(), stair_vertex), "descending staircase lost the connected lower treads");
    m = marker(tel::Shape::Rectangle); m.packet.p1 = m.packet.p2 = 16;
    m.packet.flags = tel::Filled | tel::Outline | tel::ClampToTerrain | tel::RequiredDisplay | tel::Directional | tel::Countdown;
    for (float raised : {2.0f, 4.0f, 6.0f}) {
        q.props={{tel::fixture::rectangle(1,5,1,5),0,0,raised}};
        q.sample = [raised](float x, float y, float probe) {
            const float top = x >= 1 && x <= 5 && y >= 1 && y <= 5 ? raised : 0;
            for (float z : {30.0f, top, 0.0f}) if (z <= probe) return tel::FloorSample{tel::SampleStatus::Resolved, z};
            return tel::FloorSample{tel::SampleStatus::Missing};
        };
        batch = draw(m, q, 600, options);
        check(batch.complete(), "minor rock/box interrupted a legitimate warning");
        check(std::all_of(batch.vertices.begin(), batch.vertices.end(), [raised](const auto& v) {
            const float top = v.x >= 1 && v.x <= 5 && v.y >= 1 && v.y <= 5 ? raised : 0;
            if((near(v.x,1,.0001f)||near(v.x,5,.0001f)||near(v.y,1,.0001f)||near(v.y,5,.0001f))&&
               (near(v.z,.035f)||near(v.z,raised+.035f))) return true;
            return near(v.z, top + .035f);
        }), "minor raised prop was buried beneath the marker or caught the roof");
        check(std::any_of(batch.vertices.begin(), batch.vertices.end(), [raised](const auto& v) { return near(v.z, raised + .035f); }),
              "interior obstacle was missed by corner-only tessellation");
    }
    q.sample = [](float x, float y, float probe) {
        const float slope = .3f * x + .4f * y;
        for (float z : {50.0f, slope}) if (z <= probe) return tel::FloorSample{tel::SampleStatus::Resolved, z};
        return tel::FloorSample{tel::SampleStatus::Missing};
    };
    q.regions={{tel::fixture::rectangle(-20000,20000,-20000,20000),.3,.4,0}};
    q.props.clear();
    batch = draw(m, q, 600, options);
    check(batch.complete() && std::all_of(batch.vertices.begin(), batch.vertices.end(), [](const auto& v) {
        return near(v.z, .3f * v.x + .4f * v.y + .035f);
    }), "bounded draping did not follow a continuous slope");
    q.sample = [](float, float, float probe) {
        for (float z : {20.0f, 0.0f}) if (z <= probe) return tel::FloorSample{tel::SampleStatus::Resolved, z};
        return tel::FloorSample{tel::SampleStatus::Missing};
    };
    q.regions={{tel::fixture::rectangle(-20000,20000,-20000,20000)}};
    batch = draw(m, q, 600, options);
    check(batch.complete() && std::all_of(batch.vertices.begin(), batch.vertices.end(), [](const auto& v) { return near(v.z, .035f); }),
          "bounded draping captured a separate overhead floor");
    float lift = 40;
    m.packet.z = 40;
    q.sample = [&lift](float, float, float probe) {
        for (float z : {90.0f, lift, 0.0f}) if (z <= probe) return tel::FloorSample{tel::SampleStatus::Resolved, z};
        return tel::FloorSample{tel::SampleStatus::Missing};
    };
    q.regions.front().height=40;
    const auto first = draw(m, q, 600, options); lift = 41; q.regions.front().height=41;
    batch = draw(m, q, 600, options);
    check(first.complete() && batch.complete() && near(batch.vertices.front().z - first.vertices.front().z, 1),
          "bounded draping retained a previous-frame lift height");
    m.packet.z = 0;
    q.sample = [](float x, float, float probe) {
        const float z = x >= 1 ? -20.0f : 0;
        return tel::FloorSample{z <= probe ? tel::SampleStatus::Resolved : tel::SampleStatus::Missing, z};
    };
    q.regions={{tel::fixture::rectangle(-20000,1,-20000,20000)},{tel::fixture::rectangle(1,20000,-20000,20000),0,0,-20}};
    atomic_failure(draw(m, q, 600, options), tel::GeometryStatus::Cliff, "bounded draping stretched across an unresolved cliff");
    q.sample = [](float, float, float probe) { return tel::FloorSample{probe >= 0 ? tel::SampleStatus::Resolved : tel::SampleStatus::Missing, 0}; };
    q.regions={{tel::fixture::rectangle(-20000,20000,-20000,20000)}};
    tel::GeometryBudget budget; budget.floor_query_limit = 4;
    atomic_failure(tel::build_geometry(m, 600, q, budget, options), tel::GeometryStatus::Budget, "connected terrain walk escaped its query budget");
    check(budget.floor_queries_used == 4 && !budget.vertices_used, "failed connected draping committed partial warning geometry");
}
void refined_glyphs_and_clock() {
    Queries q;
    tel::GeometryOptions options; options.enhanced_style = true;
    auto m = marker(); m.packet.p1 = 12;
    m.packet.flags = tel::Filled | tel::Outline | tel::Directional | tel::Countdown;
    auto batch = draw(m, q, 600, options);
    check(batch.complete() && has_color(batch, 0xffedf4fbu) && !has_color(batch, 0xff050505u),
          "refined glyph retained the bulky black arrow silhouette");
    check(std::all_of(batch.vertices.begin(), batch.vertices.end(), [](const auto& v) { return std::hypot(v.x, v.y) <= 12.3f; }),
          "enhanced impact clock competed with the hazard's outside boundary");
    m.packet.flags = tel::Countdown | tel::Filled;
    batch = draw(m, q, 600, options);
    check(std::all_of(batch.vertices.begin(), batch.vertices.end(), [](const auto& v) {
        return v.argb != 0xffedf4fbu || std::hypot(v.x, v.y) < 2.3f;
    }), "universal progress indicator was not a compact central clock");
    const auto white_count = [](const auto& geometry) { return std::count_if(geometry.vertices.begin(), geometry.vertices.end(), [](const auto& v) { return v.argb == 0xffedf4fbu; }); };
    check(white_count(draw(m, q, 300, options)) > white_count(batch) && white_count(batch) > white_count(draw(m, q, 900, options)),
          "compact clock did not decrease with the remaining impact time");
    check(!has_color(draw(m, q, 1100, options), 0xffedf4fbu), "compact clock survived its exact impact time");
}
void layered_warning_and_clock() {
    Queries queries;
    auto warning = marker(tel::Shape::Cone);
    warning.packet.flags = tel::Filled | tel::Outline | tel::Directional | tel::Pulse |
        tel::Countdown | tel::ClampToTerrain | tel::RequiredDisplay;
    tel::GeometryOptions options;
    options.enhanced_style = options.bounded_draping = true;
    options.layer = tel::GeometryLayer::Footprint;
    const auto early = draw(warning, queries, 300, options);
    const auto late = draw(warning, queries, 900, options);
    check(early.receipt().complete_geometry && early.receipt().terrain_conformed && has_color(early, 0xffedf4fbu),
          "A cached footprint lost its terrain presentation proof or actionable direction cue");
    check(std::equal(early.vertices.begin(), early.vertices.end(), late.vertices.begin(), late.vertices.end(),
          [](const auto& a, const auto& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.argb == b.argb; }),
          "Cached footprint depends on animated presentation time");

    options.layer = tel::GeometryLayer::Countdown;
    const auto clock = draw(warning, queries, 300, options);
    check(clock.complete() && clock.terrain_conformed && !clock.receipt().complete_geometry &&
          !clock.receipt().terrain_conformed && has_color(clock, 0xffedf4fbu),
          "Clock mesh was missing or counted as a required warning");
    check(std::all_of(clock.vertices.begin(), clock.vertices.end(), [](const auto& v) {
        return std::hypot(v.x, v.y) < 3;
    }), "Clock-only pass emitted hazard geometry");
    atomic_failure(draw(warning, queries, 1100, options), tel::GeometryStatus::NoDecoration,
                   "Clock layer emitted work after impact");
    warning.packet.flags &= ~tel::Countdown;
    atomic_failure(draw(warning, queries, 300, options), tel::GeometryStatus::NoDecoration,
                   "Clock layer emitted work for a marker without the countdown flag");
}
void bounded_draping_cost() {
    Queries q;
    tel::GeometryOptions options; options.enhanced_style = options.bounded_draping = true;
    auto m = marker();
    m.packet.p4 = 48; m.packet.flags = tel::Filled | tel::Outline | tel::Countdown | tel::Directional | tel::ClampToTerrain | tel::RequiredDisplay;
    for (float radius : {100.0f, 200.0f}) {
        m.packet.p1 = radius;
        tel::GeometryBudget budget; budget.vertex_limit = 98304;
        const auto batch = tel::build_geometry(m, 600, q, budget, options);
        std::cout << "Drape radius " << radius << ": " << batch.vertices.size() << " vertices, " << budget.floor_queries_used << " queries, " << budget.work_used << " work\n";
        check(batch.complete() && batch.terrain_conformed, "realistic large-footprint warning exceeded the modern frame allowance");
        check(budget.floor_queries_used <= 6 && budget.work_used < 2000,
              "large marker repeated a full origin walk for every triangle vertex");
    }
    m.packet.p1 = 50;
    tel::GeometryBudget shared; shared.vertex_limit = 98304; shared.required_markers_remaining = 4;
    for (unsigned n = 0; n < 4; ++n) {
        m.packet.x = static_cast<float>(n) * 120;
        const auto batch = tel::build_geometry(m, 600, q, shared, options);
        --shared.required_markers_remaining;
        check(batch.complete() && batch.terrain_conformed, "four ordinary required warnings did not share the bounded frame budget");
    }
    std::cout << "Four radius50 markers: " << shared.vertices_used << " vertices, " << shared.floor_queries_used << " queries, " << shared.work_used << " work\n";
    check(shared.floor_queries_used <= 24 && shared.work_used < 8000 && shared.vertices_used <= 98304,
          "multiple marker terrain work escaped shared accounting");
}
void off_grid_minor_props() {
    struct Prop { float x, y, length, width, angle, rise; };
    std::array<Prop, 4> props = {{{5.1f,7.3f,2,2,0,2}, {-31.7f,15.6f,5,.35f,.53f,4},
                                     {41.3f,-22.9f,7,.2f,1.17f,2}, {-15.9f,-37.1f,4,.3f,2.13f,4}}};
    for(float box_rise:{2.0f,4.0f}) {
    props[0].rise=box_rise;
    Queries q;
    std::vector<tel::fixture::Region> prop_regions;
    for(const auto& prop:props) prop_regions.push_back({tel::fixture::box(prop.x,prop.y,prop.length,prop.width,prop.angle),0,0,prop.rise});
    q.props=prop_regions;
    q.sample = [prop_regions](float x, float y, float probe) {
        float top = 0;
        for (const auto& prop : prop_regions) {
            bool inside=true;
            for(std::size_t n=0;n<prop.polygon.size();++n) if(tel::fixture::side(prop.polygon[n],prop.polygon[(n+1)%prop.polygon.size()],{x,y})<0) inside=false;
            if(inside) top=std::max(top,static_cast<float>(prop.height));
        }
        if (top <= probe) return tel::FloorSample{tel::SampleStatus::Resolved, top};
        return probe >= 0 ? tel::FloorSample{tel::SampleStatus::Resolved, 0} : tel::FloorSample{tel::SampleStatus::Missing};
    };
    auto m = marker(); m.packet.p1 = 100; m.packet.p4 = 48;
    m.packet.flags = tel::Filled | tel::ClampToTerrain | tel::RequiredDisplay;
    tel::GeometryOptions options; options.bounded_draping = true;
    if(box_rise==4) options.opacity_percent=0;
    const auto batch = draw(m, q, 600, options);
    check(batch.complete(), "off-grid low props suppressed the complete hazard footprint");
    check(std::all_of(batch.vertices.begin(),batch.vertices.end(),[](const auto& vertex){return (vertex.argb>>24)>=128;}),
          "clipped/source-projected required warning lost its minimum readable alpha");
    check(q.patches>2,"off-grid low prop incorrectly accepted the whole-square planar fast path");
    for (const auto& prop : props) {
        for (float along : {-.3f, 0.0f, .3f}) for (float across : {-.2f, 0.0f, .2f}) {
            const float x = prop.x + along * prop.length * std::cos(prop.angle) - across * prop.width * std::sin(prop.angle);
            const float y = prop.y + along * prop.length * std::sin(prop.angle) + across * prop.width * std::cos(prop.angle);
            check(covers_top(batch, x, y, prop.rise + .035f), "large footprint buried an off-grid box or rotated thin rock");
        }
    }
    }
}
void whole_patch_cache_contract() {
    auto m=marker();m.packet.p1=100;m.packet.flags=tel::Filled|tel::Outline|tel::Countdown|tel::Directional|tel::ClampToTerrain|tel::RequiredDisplay;
    tel::GeometryOptions options;options.bounded_draping=options.enhanced_style=true;
    Queries q;
    auto batch=draw(m,q,600,options);
    check(batch.complete()&&q.patches==2&&q.probes.size()==3,"flat whole-square proof was not reused for contained surfaces");
    for(const auto rejected:{tel::PatchStatus::Mismatch,tel::PatchStatus::Missing,tel::PatchStatus::Deferred}) {
        Queries fallback;
        fallback.patch_override=[rejected](const auto& triangle,std::size_t call) {
            if(call==2) return tel::SurfacePatchSample{rejected};
            return tel::fixture::certify(triangle,{{tel::fixture::rectangle(-20000,20000,-20000,20000)}});
        };
        batch=draw(m,fallback,600,options);
        check(batch.complete()&&fallback.patches>2&&fallback.probes.size()>3,"a partial/missing/deferred square proof was reused as complete coverage");
    }
    // New invocations re-sample moving geometry; no proof can survive a lift
    // update even when packet identity and marker origin are unchanged.
    float lift=2;
    q=Queries{};q.sample=[&lift](float,float,float probe){return tel::FloorSample{probe>=lift?tel::SampleStatus::Resolved:tel::SampleStatus::Missing,lift};};
    q.regions.front().height=2;
    const auto first=draw(m,q,600,options);const auto before=q.patches;lift=3;q.regions.front().height=3;
    batch=draw(m,q,600,options);
    check(first.complete()&&batch.complete()&&q.patches==before+2&&near(batch.vertices.front().z-first.vertices.front().z,1),
          "a prior-frame whole-square proof survived a moving lift");
    for(unsigned shape=1;shape<=11;++shape) {
        Queries bounds;m=marker(static_cast<tel::Shape>(shape));m.packet.flags=tel::kKnownFlags;
        options.ribbon_width=8;
        batch=draw(m,bounds,600,options);
        const float radius=(shape==4||shape==5)?4.0f:shape==6?4.0f:8.0f;
        const float extent=radius*1.15f+options.ribbon_width*4;
        check(batch.complete()&&bounds.patches==2&&std::all_of(batch.vertices.begin(),batch.vertices.end(),[extent](const auto& point) {
            return std::abs(point.x)<=extent&&std::abs(point.y)<=extent;
        }),"whole-square cache bounds omitted a supported hazard, ribbon or mechanic glyph");
    }
}
void donut_origin_hole() {
    Queries q;
    q.sample=[](float x,float y,float probe) {
        if(std::abs(x)<1&&std::abs(y)<1) return tel::FloorSample{tel::SampleStatus::Missing};
        return probe>=0?tel::FloorSample{tel::SampleStatus::Resolved,0}:tel::FloorSample{tel::SampleStatus::Missing};
    };
    q.patch_override=[](const auto& triangle,std::size_t) {
        // The actual annular footprint never enters the square origin hole.
        // No broad preflight is allowed to certify across that missing center.
        for(const auto& point:triangle) check(std::hypot(point.x,point.y)>=3.99f,"origin-hole fallback attempted to certify an unrelated missing center");
        return tel::SurfacePatchSample{tel::PatchStatus::Planar};
    };
    auto m=marker(tel::Shape::Donut);m.packet.flags=tel::Filled|tel::ClampToTerrain|tel::RequiredDisplay;
    tel::GeometryOptions options;options.bounded_draping=true;
    const auto batch=draw(m,q,600,options);
    check(batch.complete()&&batch.terrain_conformed&&!batch.vertices.empty(),"missing donut origin suppressed its valid surrounding footprint");
    check(q.patches>2&&q.probes.front().x==0&&q.probes.front().y==0,"donut origin-hole fallback invented/reused an origin floor");
    check(std::all_of(batch.vertices.begin(),batch.vertices.end(),[](const auto& point){return near(point.z,.035f);}),"donut origin fallback guessed an unverified floor height");
    q.patch_override={};
    m=marker();m.packet.flags|=tel::ClampToTerrain|tel::RequiredDisplay;
    atomic_failure(draw(m,q,600,options),tel::GeometryStatus::MissingFloor,"filled hazard guessed a floor inside its real missing footprint");
}
void source_face_cache_contract() {
    Queries q;
    q.props={{tel::fixture::box(5.1,7.3,2,2,0),0,0,4}};
    float raised=4;
    q.sample=[&raised](float x,float y,float probe) {
        const float top=x>4.1f&&x<6.1f&&y>6.3f&&y<8.3f?raised:0;
        return top<=probe?tel::FloorSample{tel::SampleStatus::Resolved,top}:tel::FloorSample{tel::SampleStatus::Missing};
    };
    std::map<std::array<float,9>,unsigned> attempts;
    q.patch_override=[&](const auto& triangle,std::size_t) {
        const auto proof=tel::fixture::certify_props(triangle,q.props);
        std::array<float,9> key{};bool whole_ground=false, whole_prop=true;
        for(std::size_t n=0;n<3;++n) {
            key[n*3]=triangle[n].x;key[n*3+1]=triangle[n].y;key[n*3+2]=triangle[n].z;
            if(std::abs(triangle[n].x)>10000||std::abs(triangle[n].y)>10000) whole_ground=true;
            const auto& point = triangle[n];
            whole_prop = whole_prop && near(point.z, raised) &&
                (near(point.x,4.1f) || near(point.x,6.1f)) && (near(point.y,6.3f) || near(point.y,8.3f));
        }
        if (whole_ground || whole_prop) ++attempts[key];
        if(whole_ground) {
            const tel::fixture::Point a{triangle[0].x,triangle[0].y},b{triangle[1].x,triangle[1].y},c{triangle[2].x,triangle[2].y},center{5.1,7.3};
            const double determinant=tel::fixture::side(a,b,c);
            const double wa=tel::fixture::side(b,c,center)/determinant,wb=tel::fixture::side(c,a,center)/determinant;
            if(wa>=0&&wb>=0&&wa+wb<=1) check(proof.status!=tel::PatchStatus::Planar,"source ground face over a hidden raised prop was falsely certified");
        }
        return proof;
    };
    auto m=marker();m.packet.x=5.1f;m.packet.y=7.3f;m.packet.p1=100;m.packet.p4=48;m.packet.flags=tel::Filled|tel::ClampToTerrain|tel::RequiredDisplay;
    tel::GeometryOptions options;options.bounded_draping=true;
    auto batch=draw(m,q,600,options);
    check(batch.complete()&&covers_top(batch,5.1f,7.3f,4.035f),"source-face cache hid the raised prop");
    check(!attempts.empty()&&std::all_of(attempts.begin(),attempts.end(),[](const auto& attempt){return attempt.second==1;}),
          "source-face certificates were never queried or repeated in one build");
    attempts.clear();
    raised=5;q.props.front().height=5;
    batch=draw(m,q,600,options);
    check(batch.complete()&&covers_top(batch,5.1f,7.3f,5.035f),"a source-face proof survived a changed raised surface");
    check(!attempts.empty()&&std::all_of(attempts.begin(),attempts.end(),[](const auto& attempt){return attempt.second==1;}),
          "source-face attempted/proven state leaked across invocations");
}
}

int main() {
    try {
        silhouettes_and_coordinates(); styles_and_palette(); glyphs_and_countdown(); anchors(); following_terrain_refines_unavailable_patches(); terrain(); budgets_and_invalid_input(); complete_supported_matrix(); enhanced_presentation(); bounded_draping(); refined_glyphs_and_clock(); layered_warning_and_clock(); bounded_draping_cost(); off_grid_minor_props(); whole_patch_cache_contract(); donut_origin_hole(); source_face_cache_contract();
        std::cout << "Telegraph geometry: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Telegraph geometry after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
