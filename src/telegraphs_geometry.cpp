#include "telegraphs_geometry.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <utility>

namespace bastion::telegraphs {
namespace {
constexpr float pi = 3.14159265358979323846f, tau = 2 * pi;
constexpr float surface_offset = 0.035f;
static_assert(kSurfaceProofTolerance < surface_offset);
constexpr unsigned maximum_depth = 18;
struct XY { float x, y; };
XY operator+(XY a, XY b) { return {a.x + b.x, a.y + b.y}; }
XY operator-(XY a, XY b) { return {a.x - b.x, a.y - b.y}; }
XY operator*(XY a, float b) { return {a.x * b, a.y * b}; }
float length2(XY a) { return a.x * a.x + a.y * a.y; }
XY xy(const TriangleVertex& v) { return {v.x, v.y}; }
std::uint32_t opacity(std::uint32_t color, float scale) {
    const auto a = static_cast<std::uint32_t>(std::clamp(static_cast<float>(color >> 24) * scale, 0.0f, 255.0f));
    return (color & 0xffffffu) | (a << 24);
}
std::uint32_t midpoint_color(std::uint32_t a, std::uint32_t b) {
    std::uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        result |= ((((a >> shift) & 255u) + ((b >> shift) & 255u)) / 2) << shift;
    return result;
}
std::uint32_t outline(std::uint32_t color) {
    return (color & 0xffffffu) | (std::min(255u, (color >> 24) + 96) << 24);
}
float marker_radius(const Packet& p) {
    if (p.shape == Shape::Rectangle || p.shape == Shape::Line)
        return std::max(p.p1 * .5f + std::abs(p.p3), p.p2 * .5f);
    if (p.shape == Shape::Cross) return std::abs(p.p3) + .5f * std::max(p.p1, p.p2);
    return p.p1;
}
int sectors(const Packet& p, float r) {
    return p.p4 >= 8 ? std::clamp(static_cast<int>(p.p4), 8, 128)
                     : std::clamp(static_cast<int>(r * 1.5f), 24, 72);
}
float pattern(Style style, int sector) {
    if (style == Style::Striped) return ((sector / 2) & 1) ? .24f : 1.0f;
    if (style == Style::Checker) return (((sector / 2) + (sector & 1)) & 1) ? .30f : 1.0f;
    if (style == Style::Gradient) return (sector & 1) ? .95f : .42f;
    return 1;
}

class Builder {
public:
    Builder(const RenderMarker& m, std::uint32_t now, GeometryQueries& q, GeometryBudget& b,
            const GeometryOptions& o) : marker_(m), p_(m.packet), now_(now), queries_(q), budget_(b), options_(o) {
        result_.tag = m.tag;
        result_.status = GeometryStatus::Complete;
        first_work_ = b.work_used;
        first_floor_query_ = b.floor_queries_used;
    }
    MarkerGeometry run() {
        if (!valid(p_) || (p_.action != Action::Create && p_.action != Action::Update) ||
            !marker_.tag.epoch || !marker_.tag.incarnation || marker_.tag.generation != p_.generation ||
            marker_.tag.id != p_.id || marker_.tag.revision != p_.revision ||
            !std::isfinite(options_.ribbon_width) || options_.ribbon_width < .01f || options_.ribbon_width > 8 ||
            options_.opacity_percent > 100 || distance(marker_.local_impact, marker_.local_start) <= 0 ||
            distance(marker_.local_impact, marker_.local_start) != distance(p_.impact_ms, p_.start_ms) ||
            distance(marker_.local_expire, marker_.local_impact) != distance(p_.expire_ms, p_.impact_ms)) return finish(GeometryStatus::Invalid);
        if (!reached(now_, marker_.local_start)) return finish(GeometryStatus::NotStarted);
        if (reached(now_, marker_.local_expire)) return finish(GeometryStatus::Expired);
        result_.covers_warning = options_.layer != GeometryLayer::Countdown;
        if (!result_.covers_warning && (!(p_.flags & Countdown) || reached(now_, marker_.local_impact)))
            return finish(GeometryStatus::NoDecoration);
        required_ = (p_.flags & RequiredDisplay) != 0;
        clamp_ = (p_.flags & ClampToTerrain) != 0;
        origin_ = {p_.x, p_.y}; reference_ = p_.z;
        float heading = p_.heading;
        if (p_.anchor != Anchor::World && (p_.flags & (FollowPosition | FollowHeading))) {
            if (!work()) return finish(result_.status);
            const auto anchor = queries_.drawn_anchor(p_.anchor == Anchor::Owner ? p_.owner_id : p_.anchor_id);
            if (anchor.status == SampleStatus::Missing) return finish(GeometryStatus::MissingAnchor);
            if (anchor.status != SampleStatus::Resolved) return finish(GeometryStatus::DeferredAnchor);
            if (p_.flags & FollowPosition) {
                if (!finite(anchor.x) || !finite(anchor.y) || !finite(anchor.z)) return finish(GeometryStatus::Numerical);
                origin_ = {anchor.x, anchor.y}; reference_ = anchor.z;
                if (clamp_ && anchor.floor_reference_valid) {
                    if (!finite(anchor.floor_z)) return finish(GeometryStatus::Numerical);
                    reference_ = anchor.floor_z;
                }
            }
            if (p_.flags & FollowHeading) {
                if (!finite(anchor.heading)) return finish(GeometryStatus::Numerical);
                heading = anchor.heading;
            }
        }
        const float angle = std::fmod(heading, 512.0f) * (tau / 512.0f);
        forward_ = {std::sin(angle), std::cos(angle)};
        lateral_ = {std::cos(angle), -std::sin(angle)};
        patch_ = options_.bounded_draping ? std::max(8.0f, marker_radius(p_) / 6) :
            std::max(16.0f, marker_radius(p_) * 1.10f / (required_ ? 2.0f : 8.0f));
        if(clamp_&&options_.bounded_draping) certify_flat_bounds();
        auto color = palette();
        if (options_.layer == GeometryLayer::Complete && (p_.flags & Pulse)) {
            const auto remaining = std::max(0, distance(marker_.local_impact, now_));
            const std::uint32_t period = options_.enhanced_style && remaining <= 2000 ? 500u : 900u;
            const auto phase = options_.enhanced_style ? static_cast<std::uint32_t>(remaining) : now_;
            color = readable(opacity(color, .88f + .12f * std::sin(tau * static_cast<float>(phase % period) / static_cast<float>(period))));
        }
        if (result_.covers_warning) footprint(color);
        if (options_.layer != GeometryLayer::Footprint) countdown(marker_radius(p_), color);
        if (result_.complete() && result_.vertices.empty())
            fail(result_.covers_warning ? GeometryStatus::Invalid : GeometryStatus::NoDecoration);
        if (result_.complete()) {
            budget_.vertices_used += result_.vertices.size();
            result_.terrain_conformed = clamp_;
        }
        return finish(result_.status);
    }
private:
    void footprint(std::uint32_t color) {
        const bool fill = (p_.flags & Filled) || !(p_.flags & (Filled | Outline));
        const bool edge = (p_.flags & Outline) || !(p_.flags & (Filled | Outline));
        const auto style = p_.style == Style::Auto
            ? ((p_.shape == Shape::Stack || p_.shape == Shape::Target) ? Style::Ring : Style::Solid) : p_.style;
        const auto segment_count = sectors(p_, p_.p1);
        switch (p_.shape) {
        case Shape::Donut: annulus(p_.p2, p_.p1, 0, tau, segment_count, color, style, fill, edge); break;
        case Shape::Cone: {
            const float half = p_.p2 * pi / 360;
            annulus(p_.p3, p_.p1, -half, half, segment_count, color, style, fill, edge); break;
        }
        case Shape::Rectangle: case Shape::Line: case Shape::Cross:
            quad(-p_.p2 * .5f, p_.p2 * .5f, p_.p3 - p_.p1 * .5f, p_.p3 + p_.p1 * .5f, color, style, fill, edge);
            if (p_.shape == Shape::Cross)
                quad(-p_.p1 * .5f, p_.p1 * .5f, p_.p3 - p_.p2 * .5f, p_.p3 + p_.p2 * .5f, color, style, fill, edge);
            break;
        default: annulus(0, p_.p1, 0, tau, segment_count, color, style, fill, edge); break;
        }
        glyphs(marker_radius(p_), color);
    }
    static bool finite(float value) { return std::isfinite(value) && std::abs(value) <= 2000000; }
    bool work() {
        if (!result_.complete()) return false;
        if (budget_.work_used >= std::min(budget_.work_limit, GeometryBudget::kMaximumWork) ||
            budget_.work_used - first_work_ >= std::min(budget_.marker_work_limit, GeometryBudget::kMaximumWork)) {
            fail(GeometryStatus::Budget); return false;
        }
        ++budget_.work_used; return true;
    }
    void fail(GeometryStatus status) { if (result_.complete()) result_.status = status; }
    MarkerGeometry finish(GeometryStatus status) {
        result_.status = status;
        if (!result_.complete()) { result_.vertices.clear(); result_.terrain_conformed = false; }
        return std::move(result_);
    }
    std::uint32_t readable(std::uint32_t color) const {
        return required_ && (color >> 24) < 128 ? (color & 0xffffffu) | 0x80000000u : color;
    }
    std::uint32_t palette() const {
        auto color = p_.color_argb;
        if (options_.unified_palette && (p_.disposition != Disposition::Auto || p_.mechanic != Mechanic::None)) color = 0;
        if (!color) {
            switch (p_.disposition) {
            case Disposition::Hostile: color = options_.hostile; break;
            case Disposition::Friendly: color = options_.friendly; break;
            case Disposition::Neutral: color = options_.neutral; break;
            case Disposition::Safe: color = options_.safe; break;
            default: break;
            }
        }
        if (!color) {
            switch (p_.mechanic) {
            case Mechanic::Stack: case Mechanic::Soak: color = 0x9850c8ffu; break;
            case Mechanic::Spread: color = 0x98c55cffu; break;
            case Mechanic::MoveAway: case Mechanic::Knockback: color = 0xa8ff9b32u; break;
            case Mechanic::Gaze: color = 0xa8e65cffu; break;
            case Mechanic::Interrupt: color = 0xb8ffdc42u; break;
            default: color = options_.hostile; break;
            }
        }
        return readable(opacity(color, static_cast<float>(options_.opacity_percent) / 100));
    }
    bool query_allowance() {
        if (!work()) return {};
        auto limit = std::min(budget_.floor_query_limit, GeometryBudget::kMaximumFloorQueries);
        if (!required_ && budget_.required_markers_remaining) limit -= std::min(limit, std::size_t(1024));
        if (budget_.floor_queries_used >= limit || budget_.floor_queries_used - first_floor_query_ >=
            std::min(budget_.marker_floor_query_limit, GeometryBudget::kMaximumFloorQueries)) {
            fail(GeometryStatus::Budget); return false;
        }
        ++budget_.floor_queries_used;
        return true;
    }
    FloorSample query_floor(XY at, float probe) {
        if (!query_allowance()) return {};
        const auto sample = queries_.floor(at.x, at.y, probe);
        if (sample.status != SampleStatus::Resolved && sample.status != SampleStatus::Missing)
            fail(GeometryStatus::DeferredFloor);
        if (sample.status == SampleStatus::Resolved && (!finite(sample.z) || sample.z > probe + .25f))
            fail(GeometryStatus::Numerical);
        return sample;
    }
    std::optional<float> reference_surface(XY at) {
        // Exact point reuse only; quantization could move a narrow cliff to the
        // wrong layer. The cache lives for one marker, not across moving lifts.
        const auto key = std::pair(std::bit_cast<std::uint32_t>(at.x), std::bit_cast<std::uint32_t>(at.y));
        if (const auto found = floors_.find(key); found != floors_.end()) return found->second;
        float probe = reference_ + 96, best = 0, delta = std::numeric_limits<float>::max();
        bool found = false;
        for (unsigned layer = 0; layer < 8; ++layer) {
            const auto sample = query_floor(at, probe);
            if (!result_.complete()) return {};
            if (sample.status == SampleStatus::Missing) break;
            if (sample.status != SampleStatus::Resolved) { fail(GeometryStatus::DeferredFloor); return {}; }
            if (!finite(sample.z) || sample.z > probe + .25f) { fail(GeometryStatus::Numerical); return {}; }
            const float difference = std::abs(sample.z - reference_);
            if (difference <= 128 && difference < delta) { delta = difference; best = sample.z; found = true; }
            if (sample.z <= reference_ + .25f) break;
            probe = sample.z - 2;
        }
        if (!found) return {};
        floors_.emplace(key, best);
        return best;
    }
    float reference_floor(XY at) {
        const auto surface=reference_surface(at);
        if(surface) return *surface;
        if(result_.complete()) fail(GeometryStatus::MissingFloor);
        return 0;
    }
    bool seed_layer(XY at) {
        if(drape_seeded_) return true;
        std::optional<float> ground;
        if(!origin_missing_) ground=reference_surface(origin_);
        if(!result_.complete()) return false;
        drape_origin_=origin_;
        if(!ground) {
            origin_missing_=true;
            // The origin may lie in the deliberately empty center of a donut.
            // An optional broad proof must not invent a floor there or hide
            // valid geometry on the surrounding footprint.
            if(at.x==origin_.x&&at.y==origin_.y) return false;
            ground=reference_surface(at);
            if(!result_.complete()) return false;
            if(!ground) {fail(GeometryStatus::MissingFloor);return false;}
            drape_origin_=at;
        }
        const auto top=query_floor(drape_origin_,*ground+minor_step);
        if(!result_.complete()) return false;
        if(top.status!=SampleStatus::Resolved) {fail(GeometryStatus::MissingFloor);return false;}
        seed_z_=top.z;drape_seeded_=true;
        drape_grid_.emplace(std::pair(0,0),seed_z_);
        return true;
    }
    float floor(XY at) {
        if (!options_.bounded_draping) return reference_floor(at);
        const auto key = std::pair(std::bit_cast<std::uint32_t>(at.x), std::bit_cast<std::uint32_t>(at.y));
        if (const auto found = draped_.find(key); found != draped_.end()) return found->second;
        // Seed the encounter's actual layer once. A low rock/box can sit over
        // that floor; take its top within six EQ units, never an arbitrary
        // higher floor from the old wide reference-layer search.
        if (!drape_seeded_) {
            if(!seed_layer(at)) {if(result_.complete()) fail(GeometryStatus::MissingFloor);return 0;}
        }
        // A wide point probe resolves a candidate top at this XY only. Whole
        // triangle interiors still require native surface certificates. Nearby
        // tops need no grid walk; higher treads/roofs use local continuity.
        if (!drape_layered_) {
            const auto direct = query_floor(at, seed_z_ + 96);
            if (!result_.complete()) return 0;
            if (direct.status == SampleStatus::Missing) { fail(GeometryStatus::MissingFloor); return 0; }
            if (std::abs(direct.z - seed_z_) <= minor_step + .01f) {
                draped_.emplace(key, direct.z);
                return direct.z;
            }
            // Once a marker crosses layered/elevated geometry, stop repeating
            // the rejected wide probe at every vertex. The connected grid is
            // then the single layer authority for the remainder of this mesh.
            drape_layered_ = true;
        }
        // Resolve a connected two-EQ grid rooted at the encounter layer. A
        // deterministic parent for each cell shares already walked paths
        // between vertices; large markers do not walk origin-to-vertex again
        // for each triangle. Local reference heights allow cumulative stairs.
        const XY delta = at - drape_origin_;
        auto cell = std::pair(static_cast<int>(std::round(delta.x / walk_spacing)),
                              static_cast<int>(std::round(delta.y / walk_spacing)));
        std::vector<std::pair<int, int>> pending;
        auto current = cell;
        while (!drape_grid_.contains(current)) {
            if (!work()) return 0;
            pending.push_back(current);
            if (std::abs(current.first) >= std::abs(current.second)) current.first += current.first > 0 ? -1 : 1;
            else current.second += current.second > 0 ? -1 : 1;
        }
        float previous = drape_grid_.at(current);
        for (auto next = pending.rbegin(); next != pending.rend(); ++next) {
            const XY point = drape_origin_ + XY{static_cast<float>(next->first) * walk_spacing, static_cast<float>(next->second) * walk_spacing};
            previous = local_floor(point, previous);
            if (!result_.complete()) return 0;
            drape_grid_.emplace(*next, previous);
        }
        const XY cell_at = drape_origin_ + XY{static_cast<float>(cell.first) * walk_spacing, static_cast<float>(cell.second) * walk_spacing};
        const float height = at.x == cell_at.x && at.y == cell_at.y ? previous : local_floor(at, previous);
        draped_.emplace(key, height);
        return height;
    }
    float local_floor(XY point, float previous) {
        const auto sample = query_floor(point, previous + minor_step);
        if (!result_.complete()) return 0;
        if (sample.status != SampleStatus::Resolved) { fail(GeometryStatus::MissingFloor); return 0; }
        // A large drop cannot be silently spanned. Small vertical treads remain
        // eligible for exact native-face clipping at the surface boundary.
        if (std::abs(sample.z - previous) > minor_step + .01f) { fail(GeometryStatus::Cliff); return 0; }
        return sample.z;
    }
    TriangleVertex vertex(XY at, std::uint32_t color) {
        if (!result_.complete()) return {};
        if (!finite(at.x) || !finite(at.y)) { fail(GeometryStatus::Numerical); return {}; }
        const float z = (clamp_ ? (inside_flat_bounds(at)?flat_z_:floor(at)) : reference_) + p_.p5 + surface_offset;
        if (!finite(z)) fail(GeometryStatus::Numerical);
        return {at.x, at.y, z, readable(color)};
    }
    TriangleVertex middle(const TriangleVertex& a, const TriangleVertex& b) {
        return vertex((xy(a) + xy(b)) * .5f, midpoint_color(a.argb, b.argb));
    }
    XY world(XY local) const { return origin_ + lateral_ * local.x + forward_ * local.y; }
    bool inside_flat_bounds(XY point) const {
        return flat_bounds_&&point.x>=flat_min_.x&&point.x<=flat_max_.x&&point.y>=flat_min_.y&&point.y<=flat_max_.y;
    }
    void certify_flat_bounds() {
        // Two native whole-patch proofs cover every possible rendered vertex,
        // including outline widths, glyph heads and the raw outer countdown.
        // This is a current-invocation proof cache, never a sampled flat guess.
        if(!seed_layer(origin_)) return;
        flat_z_=floor(origin_);
        if(!result_.complete()) return;
        const float extent=marker_radius(p_)*1.15f+options_.ribbon_width*4;
        flat_min_={origin_.x-extent,origin_.y-extent};flat_max_={origin_.x+extent,origin_.y+extent};
        if(!finite(flat_min_.x)||!finite(flat_min_.y)||!finite(flat_max_.x)||!finite(flat_max_.y)) return;
        const std::array<TriangleVertex,4> corners={{{flat_min_.x,flat_min_.y,flat_z_,0},{flat_max_.x,flat_min_.y,flat_z_,0},
                                                  {flat_max_.x,flat_max_.y,flat_z_,0},{flat_min_.x,flat_max_.y,flat_z_,0}}};
        for(const auto indices:{std::array<std::size_t,3>{0,1,2},std::array<std::size_t,3>{0,2,3}}) {
            if(!query_allowance()) return;
            const auto patch=queries_.surface_patch({corners[indices[0]],corners[indices[1]],corners[indices[2]]},minor_step);
            if(patch.status!=PatchStatus::Planar) return;
        }
        flat_bounds_=true;
    }
    void emit(const TriangleVertex& a, const TriangleVertex& b, const TriangleVertex& c) {
        if (!result_.complete()) return;
        // Float coordinates can collapse a narrow ribbon or tiny hazard at a
        // large EQ origin. Nonempty but zero-area triangles cannot certify
        // complete geometry; reject the entire marker rather than lose edges.
        const double area2 = (static_cast<double>(b.x) - a.x) * (static_cast<double>(c.y) - a.y) -
                             (static_cast<double>(b.y) - a.y) * (static_cast<double>(c.x) - a.x);
        if (area2 == 0 || !std::isfinite(area2)) { fail(GeometryStatus::Numerical); return; }
        auto limit = std::min(budget_.vertex_limit, GeometryBudget::kMaximumVertices);
        if (!required_ && budget_.required_markers_remaining) limit -= std::min(limit, GeometryBudget::kMaximumVertices / 3);
        if (budget_.vertices_used > limit || result_.vertices.size() > limit - budget_.vertices_used ||
            limit - budget_.vertices_used - result_.vertices.size() < 3) { fail(GeometryStatus::Budget); return; }
        result_.vertices.insert(result_.vertices.end(), {a, b, c});
    }
    void triangle(TriangleVertex a, TriangleVertex b, TriangleVertex c, unsigned depth = 0) {
        if (!work()) return;
        if(depth&&options_.bounded_draping&&clipping_sliver(a,b,c)) return;
        if (!clamp_) { emit(a, b, c); return; }
        if(inside_flat_bounds(xy(a))&&inside_flat_bounds(xy(b))&&inside_flat_bounds(xy(c))&&
           std::abs(a.z-flat_z_-p_.p5-surface_offset)<=.002f&&std::abs(b.z-flat_z_-p_.p5-surface_offset)<=.002f&&
           std::abs(c.z-flat_z_-p_.p5-surface_offset)<=.002f) {emit(a,b,c);return;}
        if(project_from_proof(a,b,c)) return;
        SurfacePatchSample patch;
        if (options_.bounded_draping) {
            if (!query_allowance()) return;
            auto floor_vertices = std::array{a, b, c};
            for (auto& point : floor_vertices) point.z -= p_.p5 + surface_offset;
            patch = queries_.surface_patch(floor_vertices, minor_step);
            if (patch.status == PatchStatus::Planar) { emit(a, b, c); return; }
            if (patch.status != PatchStatus::Mismatch && patch.status != PatchStatus::Missing) { fail(GeometryStatus::DeferredFloor); return; }
            if (!finite(patch.x) || !finite(patch.y) || (patch.height_known && !finite(patch.z))) { fail(GeometryStatus::Numerical); return; }
            const std::array<TriangleVertex,3> corners={a,b,c};
            const std::array<float,3> elevation={std::abs(a.z-b.z),std::abs(b.z-c.z),std::abs(c.z-a.z)};
            const auto edge=static_cast<unsigned>(std::max_element(elevation.begin(),elevation.end())-elevation.begin());
            if(elevation[edge]>minor_step+.01f) {
                if(depth>=28) {fail(GeometryStatus::Cliff);return;}
                const auto mid=middle(corners[edge],corners[(edge+1)%3]);
                triangle(corners[edge],mid,corners[(edge+2)%3],depth+1);
                triangle(mid,corners[(edge+1)%3],corners[(edge+2)%3],depth+1);
                return;
            }
            if (patch.face_known) {
                certify_source_face(patch.face,{a,b,c});
                if(!result_.complete()) return;
                if(project_from_proof(a,b,c)) return;
                if(!patch.region_count) {
                    const float selected=floor({patch.x,patch.y});
                    if(!result_.complete()) return;
                    if(patch.height_known&&std::abs(selected-patch.z)>.005f) {
                        const auto barycentric=weights({a,b,c},{patch.x,patch.y});
                        if(depth>=48||*std::min_element(barycentric.begin(),barycentric.end())<=0) {fail(GeometryStatus::Numerical);return;}
                        auto witness=vertex({patch.x,patch.y},midpoint_color(midpoint_color(a.argb,b.argb),c.argb));
                        triangle(a,b,witness,depth+1);triangle(b,c,witness,depth+1);triangle(c,a,witness,depth+1);
                        return;
                    }
                }
                if (depth >= 48 || !project_face({a,b,c},patch,depth)) fail(GeometryStatus::Numerical);
                return;
            }
        }
        const std::array<float, 3> sizes = {length2(xy(a) - xy(b)), length2(xy(b) - xy(c)), length2(xy(c) - xy(a))};
        const auto longest = static_cast<unsigned>(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());
        unsigned split = longest;
        bool needed = sizes[longest] > patch_ * patch_;
        TriangleVertex midpoint;
        std::array<TriangleVertex, 3> v = {a, b, c};
        if (options_.bounded_draping && !needed) {
            // Interior obstacles are invisible to a corner-only sampler. Check
            // all edges and the interior even on flat-looking triangles.
            float deviation = 0;
            for (unsigned edge = 0; edge < 3; ++edge) {
                const auto mid = middle(v[edge], v[(edge + 1) % 3]);
                const float error = std::abs(mid.z - (v[edge].z + v[(edge + 1) % 3].z) * .5f);
                if (error > deviation) { deviation = error; split = edge; }
            }
            const auto center = vertex((xy(a) + xy(b) + xy(c)) * (1.0f / 3),
                midpoint_color(midpoint_color(a.argb, b.argb), c.argb));
            if (!result_.complete()) return;
            const float center_error = std::abs(center.z - (a.z + b.z + c.z) / 3);
            needed = deviation > .30f && sizes[split] > .75f * .75f;
            if (center_error > .30f && center_error >= deviation) {
                split = longest;
                needed = sizes[longest] > .75f * .75f;
                if (!needed) {
                    triangle(a, b, center, depth + 1);
                    triangle(b, c, center, depth + 1);
                    triangle(c, a, center, depth + 1);
                    return;
                }
            }
            if (!needed) {
                if (deviation > .30f && split_step(a, b, c, depth)) return;
                // Point samples can miss an arbitrarily thin interior prop.
                // Only the native whole-patch certificate permits a planar
                // span; use its actual interior witness when samples agree.
                const double determinant = (static_cast<double>(b.y) - c.y) * (a.x - c.x) +
                                           (static_cast<double>(c.x) - b.x) * (a.y - c.y);
                if (determinant == 0) { fail(GeometryStatus::Numerical); return; }
                const double wa = ((static_cast<double>(b.y) - c.y) * (patch.x - c.x) + (static_cast<double>(c.x) - b.x) * (patch.y - c.y)) / determinant;
                const double wb = ((static_cast<double>(c.y) - a.y) * (patch.x - c.x) + (static_cast<double>(a.x) - c.x) * (patch.y - c.y)) / determinant;
                const double wc = 1 - wa - wb;
                if (wa <= 0 || wb <= 0 || wc <= 0 || depth >= 28) { fail(GeometryStatus::Numerical); return; }
                auto witness = vertex({patch.x, patch.y}, midpoint_color(midpoint_color(a.argb, b.argb), c.argb));
                if (!result_.complete()) return;
                const double plane = wa * a.z + wb * b.z + wc * c.z;
                if (std::abs(witness.z - plane) <= .002) { fail(GeometryStatus::DeferredFloor); return; }
                triangle(a, b, witness, depth + 1); triangle(b, c, witness, depth + 1); triangle(c, a, witness, depth + 1);
                return;
            }
            // Always shorten the longest edge: following only the edge with
            // greatest Z error can keep producing a sliver on an exact riser.
            split = longest;
        }
        if (!options_.bounded_draping && !needed) {
            const std::array<float, 3> dz = {std::abs(a.z - b.z), std::abs(b.z - c.z), std::abs(c.z - a.z)};
            split = static_cast<unsigned>(std::max_element(dz.begin(), dz.end()) - dz.begin());
            if (dz[split] > 3) {
                midpoint = middle(v[split], v[(split + 1) % 3]);
                needed = std::abs(midpoint.z - (v[split].z + v[(split + 1) % 3].z) * .5f) > .75f;
                if (needed && sizes[split] <= 1.5f * 1.5f) { fail(GeometryStatus::Cliff); return; }
            }
        }
        if (!result_.complete()) return;
        if (!needed) { emit(a, b, c); return; }
        if (depth >= (options_.bounded_draping ? 28u : maximum_depth)) { fail(GeometryStatus::Cliff); return; }
        // Midpoints, including line ribbon corners, always receive their own
        // floor sample. Never interpolate Z across different terrain layers.
        midpoint = middle(v[split], v[(split + 1) % 3]);
        triangle(v[split], midpoint, v[(split + 2) % 3], depth + 1);
        triangle(midpoint, v[(split + 1) % 3], v[(split + 2) % 3], depth + 1);
    }
    struct ClipPoint { double x=0,y=0; };
    using Polygon=std::vector<ClipPoint>;
    static double side(ClipPoint a,ClipPoint b,ClipPoint point) { return (b.x-a.x)*(point.y-a.y)-(b.y-a.y)*(point.x-a.x); }
    static double polygon_area(const Polygon& polygon) {
        double sum=0;
        for(std::size_t n=1;n+1<polygon.size();++n) sum+=side(polygon[0],polygon[n],polygon[n+1]);
        return std::abs(sum)*.5;
    }
    template<class Distance> Polygon clip(const Polygon& polygon,Distance distance) {
        Polygon output;
        if(polygon.empty()) return output;
        auto previous=polygon.back();double prior=distance(previous);
        for(const auto current:polygon) {
            if(!work()) return {};
            const double next=distance(current);
            if((prior>=0)!=(next>=0)) {
                const double fraction=prior/(prior-next);
                output.push_back({previous.x+(current.x-previous.x)*fraction,previous.y+(current.y-previous.y)*fraction});
            }
            if(next>=0) output.push_back(current);
            previous=current;prior=next;
        }
        output.erase(std::unique(output.begin(),output.end(),[](ClipPoint a,ClipPoint b){const double dx=a.x-b.x,dy=a.y-b.y;return dx*dx+dy*dy<1e-24;}),output.end());
        if(output.size()>1) {const double dx=output.front().x-output.back().x,dy=output.front().y-output.back().y;if(dx*dx+dy*dy<1e-24) output.pop_back();}
        return output;
    }
    static std::array<double,3> weights(const std::array<TriangleVertex,3>& plane,ClipPoint at) {
        const ClipPoint a{plane[0].x,plane[0].y},b{plane[1].x,plane[1].y},c{plane[2].x,plane[2].y};
        const double determinant=side(a,b,c);
        const double wa=side(b,c,at)/determinant,wb=side(c,a,at)/determinant;
        return {wa,wb,1-wa-wb};
    }
    static double plane_height(const std::array<TriangleVertex,3>& plane,ClipPoint at) {
        const auto barycentric=weights(plane,at);
        return barycentric[0]*plane[0].z+barycentric[1]*plane[1].z+barycentric[2]*plane[2].z;
    }
    struct FaceProof {
        std::array<TriangleVertex,3> face{};
        float certified_rise=0;
    };
    bool project_from_proof(TriangleVertex a,TriangleVertex b,TriangleVertex c) {
        for(const auto& proof:face_cache_) {
            if(proof.certified_rise==0) continue;
            auto candidate=std::array{a,b,c};bool contained=true;
            for(auto& point:candidate) {
                const auto barycentric=weights(proof.face,{point.x,point.y});
                const double floor_z=plane_height(proof.face,{point.x,point.y});
                const double ceiling=static_cast<double>(point.z)-p_.p5-surface_offset+minor_step;
                if(*std::min_element(barycentric.begin(),barycentric.end())<0||floor_z>ceiling||ceiling>floor_z+proof.certified_rise) contained=false;
                point.z=static_cast<float>(floor_z+p_.p5+surface_offset);
            }
            // Both planes are affine: the corner ceiling inequalities prove
            // the entire contained triangle uses the same certified layer.
            if(contained) {emit(candidate[0],candidate[1],candidate[2]);return true;}
        }
        return false;
    }
    void certify_source_face(const std::array<TriangleVertex,3>& face,const std::array<TriangleVertex,3>& candidate) {
        for(const auto& point:face) if(!finite(point.x)||!finite(point.y)||!finite(point.z)) return;
        if(side({face[0].x,face[0].y},{face[1].x,face[1].y},{face[2].x,face[2].y})==0) return;
        const auto same=[&](const auto& prior) {
            for(std::size_t n=0;n<face.size();++n) if(prior[n].x!=face[n].x||prior[n].y!=face[n].y||prior[n].z!=face[n].z) return false;
            return true;
        };
        double extra=0;
        for(const auto& point:candidate) extra=std::max(extra,static_cast<double>(point.z)-p_.p5-surface_offset-plane_height(face,{point.x,point.y}));
        const double wanted=std::ceil(minor_step+extra);
        if(!std::isfinite(wanted)||wanted>128) return;
        const float rise=static_cast<float>(wanted);
        if (face_cache_.size() >= 256 || std::any_of(face_cache_.begin(), face_cache_.end(), [&](const auto& proof) {
            return same(proof.face);
        })) return;
        // Try this broad shortcut once. Child patches can still prove a
        // smaller area or a different ceiling when this proof cannot cover it.
        face_cache_.push_back({face});
        if(!query_allowance()) return;
        if (queries_.surface_patch(face, rise).status == PatchStatus::Planar) face_cache_.back().certified_rise = rise;
    }
    static std::optional<ClipPoint> rounded_inside(const Polygon& polygon, ClipPoint at, ClipPoint center) {
        const auto inside = [&](ClipPoint point) {
            for (std::size_t n = 0; n < polygon.size(); ++n)
                if (side(polygon[n], polygon[(n + 1) % polygon.size()], point) < 0) return false;
            return true;
        };
        const auto round = [](ClipPoint point) {
            return ClipPoint{static_cast<float>(point.x), static_cast<float>(point.y)};
        };
        const auto point = round(at);
        if (inside(point)) return point;
        const float x = static_cast<float>(point.x), y = static_cast<float>(point.y);
        const float low = -std::numeric_limits<float>::infinity(), high = std::numeric_limits<float>::infinity();
        const std::array xs{x, std::nextafter(x, low), std::nextafter(x, high)};
        const std::array ys{y, std::nextafter(y, low), std::nextafter(y, high)};
        ClipPoint best = point;
        double nearest = std::numeric_limits<double>::infinity();
        for (const auto rx : xs) {
            for (const auto ry : ys) {
                const double dx = rx - at.x, dy = ry - at.y;
                const double distance = dx * dx + dy * dy;
                if (distance < nearest && inside({rx, ry})) { nearest = distance; best = {rx, ry}; }
            }
        }
        if (nearest < std::numeric_limits<double>::infinity()) return best;

        // At very acute corners there may be no adjacent representable point.
        // Walk toward the polygon interior instead of oscillating across edges.
        for (double factor = 1e-7; factor < 1; factor *= 2) {
            const auto candidate = round({at.x + (center.x - at.x) * factor, at.y + (center.y - at.y) * factor});
            if (inside(candidate)) return candidate;
        }
        const auto midpoint = round(center);
        return inside(midpoint) ? std::optional{midpoint} : std::nullopt;
    }
    void projected_polygon(const Polygon& polygon,const std::array<TriangleVertex,3>& color_plane,
                           const std::array<TriangleVertex,3>* height_plane,unsigned depth) {
        if(!result_.complete()||polygon.size()<3||polygon_area(polygon)<1e-10) return;
        std::vector<TriangleVertex> vertices;vertices.reserve(polygon.size());
        ClipPoint center{};for(const auto point:polygon) {center.x+=point.x;center.y+=point.y;}
        center.x/=static_cast<double>(polygon.size());center.y/=static_cast<double>(polygon.size());
        for(const auto at:polygon) {
            const auto rounded = rounded_inside(polygon, at, center);
            if (!rounded) continue;
            TriangleVertex point{static_cast<float>(rounded->x),static_cast<float>(rounded->y),0,0};
            // The remainder has no certified plane. Sample its new vertices
            // instead of carrying the parent's interpolated height over a step.
            const double surface = height_plane ? plane_height(*height_plane, *rounded) : floor(xy(point));
            point.z = static_cast<float>(surface + p_.p5 + surface_offset);
            const auto barycentric=weights(color_plane,*rounded);
            for(unsigned shift=0;shift<32;shift+=8) {
                double channel=0;for(std::size_t n=0;n<3;++n) channel+=barycentric[n]*static_cast<double>((color_plane[n].argb>>shift)&255u);
                point.argb|=static_cast<std::uint32_t>(std::clamp(channel,0.0,255.0))<<shift;
            }
            point.argb=readable(point.argb);
            vertices.push_back(point);
        }
        for(std::size_t n=1;n+1<vertices.size();++n) step_emit(vertices[0],vertices[n],vertices[n+1],depth);
    }
    bool project_face(const std::array<TriangleVertex,3>& candidate,const SurfacePatchSample& patch,unsigned depth) {
        const auto& face=patch.face;
        for(const auto& point:face) if(!finite(point.x)||!finite(point.y)||!finite(point.z)) return false;
        const ClipPoint a{face[0].x,face[0].y},b{face[1].x,face[1].y},c{face[2].x,face[2].y};
        const double face_area=side(a,b,c);if(face_area==0) return false;
        Polygon mask={a,b,c};if(face_area<0) std::reverse(mask.begin(),mask.end());
        if(patch.region_count) {
            if(patch.region_count<3||patch.region_count>patch.region.size()) return false;
            Polygon region;
            for(std::size_t n=0;n<patch.region_count;++n) {
                const auto& point=patch.region[n];
                if(!std::isfinite(point[0])||!std::isfinite(point[1])||std::abs(point[0])>2000000||std::abs(point[1])>2000000) return false;
                region.push_back({point[0],point[1]});
            }
            double winding=0;for(std::size_t n=1;n+1<region.size();++n) winding+=side(region[0],region[n],region[n+1]);
            if(winding==0) return false;
            if(winding<0) std::reverse(region.begin(),region.end());
            ClipPoint center{};for(const auto point:region) {center.x+=point.x;center.y+=point.y;}
            center.x/=static_cast<double>(region.size());center.y/=static_cast<double>(region.size());
            const auto centroid=weights(candidate,center);
            if(*std::min_element(centroid.begin(),centroid.end())<=0) return false;
            for(std::size_t n=0;n<region.size();++n) {
                const auto start=region[n],end=region[(n+1)%region.size()];
                mask=clip(mask,[&](ClipPoint at){return side(start,end,at);});
            }
        }
        auto floor_candidate=candidate;for(auto& point:floor_candidate) point.z-=p_.p5+surface_offset;
        mask=clip(mask,[&](ClipPoint at){return minor_step-(plane_height(face,at)-plane_height(floor_candidate,at));});
        Polygon remaining={{candidate[0].x,candidate[0].y},{candidate[1].x,candidate[1].y},{candidate[2].x,candidate[2].y}};
        if(side(remaining[0],remaining[1],remaining[2])<0) std::reverse(remaining.begin(),remaining.end());
        Polygon overlap=remaining;
        for(std::size_t n=0;n<mask.size();++n) {
            const auto start=mask[n],end=mask[(n+1)%mask.size()];overlap=clip(overlap,[&](ClipPoint at){return side(start,end,at);});
        }
        if(mask.size()<3||polygon_area(overlap)==0) return false;
        // Split exactly at renderer face edges. Each side keeps its certified
        // plane, so a vertical step never becomes an interpolated ramp.
        for(std::size_t n=0;n<mask.size()&&!remaining.empty();++n) {
            const auto start=mask[n],end=mask[(n+1)%mask.size()];
            if((end.x-start.x)*(end.x-start.x)+(end.y-start.y)*(end.y-start.y)<1e-24) continue;
            const auto outside=clip(remaining,[&](ClipPoint at){return -side(start,end,at);});
            projected_polygon(outside,candidate,nullptr,depth+1);
            remaining=clip(remaining,[&](ClipPoint at){return side(start,end,at);});
        }
        projected_polygon(remaining,candidate,&face,depth+1);
        return result_.complete();
    }
    std::pair<TriangleVertex, TriangleVertex> step_boundary(TriangleVertex a, TriangleVertex b) {
        // Two separate samples hug the discontinuity, one on each top. A
        // sloped triangle between them would bury the upper tread and create
        // sawtooth holes with real depth testing. No artificial Z inflation or
        // guessed vertical face is used.
        const float threshold = (a.z + b.z) * .5f;
        const bool lower = a.z < b.z;
        for (unsigned n = 0; n < 9 && result_.complete(); ++n) {
            const auto mid = middle(a, b);
            if ((mid.x == a.x && mid.y == a.y) || (mid.x == b.x && mid.y == b.y)) break;
            if ((mid.z < threshold) == lower) a = mid;
            else b = mid;
        }
        return {a, b};
    }
    bool split_step(const TriangleVertex& a, const TriangleVertex& b, const TriangleVertex& c, unsigned depth) {
        const std::array<TriangleVertex, 3> vertices = {a, b, c};
        for (unsigned n = 0; n < 3; ++n) {
            const auto single = vertices[n], next = vertices[(n + 1) % 3], last = vertices[(n + 2) % 3];
            if (std::abs(single.z - next.z) <= 1 || std::abs(single.z - last.z) <= 1 || std::abs(next.z - last.z) > 1) continue;
            const auto first = step_boundary(single, next), second = step_boundary(single, last);
            if (!result_.complete()) return true;
            step_emit(single, first.first, second.first, depth + 1);
            step_emit(first.second, next, last, depth + 1);
            step_emit(first.second, last, second.second, depth + 1);
            return true;
        }
        return false;
    }
    void step_emit(const TriangleVertex& a, const TriangleVertex& b, const TriangleVertex& c, unsigned depth) {
        // A split exactly through an existing corner can leave an empty side.
        // Only clipping-generated empty polygons are discarded; original
        // collapsed hazard/ribbon triangles still fail the numerical check.
        if(!clipping_sliver(a,b,c)) triangle(a,b,c,depth);
    }
    static bool clipping_sliver(const TriangleVertex& a,const TriangleVertex& b,const TriangleVertex& c) {
        const double area = (static_cast<double>(b.x) - a.x) * (static_cast<double>(c.y) - a.y) -
                            (static_cast<double>(b.y) - a.y) * (static_cast<double>(c.x) - a.x);
        float coordinate=0;
        for(float value:{a.x,a.y,b.x,b.y,c.x,c.y}) coordinate=std::max(coordinate,std::abs(value));
        const float resolution=std::nextafter(coordinate,std::numeric_limits<float>::infinity())-coordinate;
        const double edge=std::sqrt(std::max({length2(xy(a)-xy(b)),length2(xy(b)-xy(c)),length2(xy(c)-xy(a))}));
        // Clipping can produce two copies of an edge separated by less than
        // the float coordinates can represent. Drop only those rounding-sized
        // slivers, not narrow original props or authoritative hazard triangles.
        return std::abs(area)<=std::max(1e-8,static_cast<double>(resolution)*edge*2);
    }
    void tri(XY a, XY b, XY c, std::uint32_t ca, std::uint32_t cb, std::uint32_t cc) {
        if (!result_.complete()) return;
        triangle(vertex(world(a), ca), vertex(world(b), cb), vertex(world(c), cc));
    }
    void line(XY a, XY b, std::uint32_t color, float width_scale = 1) {
        if (!result_.complete()) return;
        const auto d = b - a;
        const float n = std::sqrt(length2(d));
        if (n <= .00001f) return;
        const XY normal = XY{-d.y, d.x} * (options_.ribbon_width * width_scale * .5f / n);
        tri(a + normal, a - normal, b - normal, color, color, color);
        tri(a + normal, b - normal, b + normal, color, color, color);
    }
    void edge_line(XY a, XY b, std::uint32_t color) {
        if (options_.enhanced_style) line(a, b, 0x90131d2au, 1.9f);
        line(a, b, color);
    }
    void annulus(float inner, float outer, float begin, float end, int count, std::uint32_t color,
                 Style style, bool fill, bool edge) {
        const auto polar = [](float angle, float r) { return XY{std::sin(angle) * r, std::cos(angle) * r}; };
        const bool disk = inner <= 0;
        const auto edge_color = outline(color);
        for (int sector = 0; sector < count && result_.complete(); ++sector) {
            const float a = begin + (end - begin) * static_cast<float>(sector) / static_cast<float>(count);
            const float b = begin + (end - begin) * static_cast<float>(sector + 1) / static_cast<float>(count);
            const auto oa = polar(a, outer), ob = polar(b, outer), ia = polar(a, inner), ib = polar(b, inner);
            const auto shade = opacity(color, pattern(style, sector));
            if (fill) {
                if (disk) tri({0, 0}, oa, ob, (style == Style::Gradient || style == Style::Ring) ? opacity(color, .18f) : shade, shade, shade);
                else {
                    const auto inside = style == Style::Gradient ? opacity(shade, .45f) : shade;
                    tri(ia, oa, ob, inside, shade, shade); tri(ia, ob, ib, inside, shade, inside);
                }
            }
            if (edge) { edge_line(oa, ob, edge_color); if (!disk) edge_line(ia, ib, edge_color); }
            if (style == Style::Ring && disk && !options_.enhanced_style) line(polar(a, outer * .72f), polar(b, outer * .72f), edge_color);
        }
        if (edge && end - begin < tau - .001f) {
            edge_line(polar(begin, inner), polar(begin, outer), edge_color);
            edge_line(polar(end, inner), polar(end, outer), edge_color);
        }
    }
    void quad(float left, float right, float back, float front, std::uint32_t color, Style style, bool fill, bool edge) {
        if (fill) {
            const auto low = style == Style::Gradient ? opacity(color, .35f) : color;
            tri({left, back}, {right, back}, {right, front}, low, color, color);
            tri({left, back}, {right, front}, {left, front}, low, color, low);
            if (style == Style::Striped || style == Style::Checker) {
                const int bands = std::clamp(static_cast<int>((front - back) / 4), 2, 16);
                for (int i = 1; i < bands && result_.complete(); i += 2) {
                    const float a = back + (front - back) * static_cast<float>(i) / static_cast<float>(bands);
                    const float b = back + (front - back) * static_cast<float>(i + 1) / static_cast<float>(bands);
                    quad(left, right, a, b, opacity(color, .18f), Style::Solid, true, false);
                }
            }
        }
        if (edge) {
            const auto c = outline(color);
            edge_line({left, back}, {right, back}, c); edge_line({right, back}, {right, front}, c);
            edge_line({right, front}, {left, front}, c); edge_line({left, front}, {left, back}, c);
        }
    }
    void silhouette(XY from, XY to, float head, float width, float shaft, std::uint32_t color) {
        const auto d = to - from;
        const float len = std::sqrt(length2(d));
        if (len <= .001f) return;
        const XY along = d * (1 / len), across = {-along.y, along.x};
        head = std::clamp(head, len * .22f, len * .68f);
        width = std::min(width, len * .55f); shaft = std::min(shaft, width * .72f);
        const XY base = to - along * head, overlap = base + along * (head * .12f);
        tri(to, base + across * width, base - across * width, color, color, color);
        tri(from + across * shaft, from - across * shaft, overlap - across * shaft, color, color, color);
        tri(from + across * shaft, overlap - across * shaft, overlap + across * shaft, color, color, color);
    }
    void arrow(XY from, XY to, float wing) {
        const auto d = to - from;
        const float len = std::sqrt(length2(d));
        if (len <= .001f) return;
        if (options_.enhanced_style) {
            const XY along = d * (1 / len), across{-along.y, along.x};
            const float head = std::min(wing * .9f, len * .46f);
            const float width = head * .65f;
            const XY base = to - along * head;
            const float stroke = std::clamp(wing * .18f, .28f, .50f) / options_.ribbon_width;
            for (unsigned layer = 0; layer < 2; ++layer) {
                const auto ink = layer ? 0xffedf4fbu : 0xa0142030u;
                const float scale = stroke * (layer ? 1 : 1.8f);
                line(from, to - along * (head * .12f), ink, scale);
                line(base + across * width, to, ink, scale);
                line(to, base - across * width, ink, scale);
            }
            return;
        }
        const float h = std::clamp(wing * 1.25f, len * .34f, len * .66f);
        const float w = std::max(wing * .95f, len * .20f), s = std::max(.70f, wing * .42f);
        silhouette(from, to, h * 1.10f, w * 1.48f, s * 1.72f, 0xffffffffu);
        silhouette(from + d * .045f, to - d * .045f, h * .92f, w * .76f, s * .82f, 0xff050505u);
    }
    void pips(float r) {
        if (options_.enhanced_style) {
            const float radius = std::clamp(r * .075f, .5f, 1.1f);
            for (int i = 0; i < 4 && result_.complete(); ++i) {
                const float angle = static_cast<float>(i) * pi * .5f;
                const XY at{std::sin(angle) * r * .65f, std::cos(angle) * r * .65f};
                for (unsigned layer = 0; layer < 2; ++layer) {
                    for (unsigned side = 0; side < 16; ++side) {
                        const float a = static_cast<float>(side) * tau / 16, b = static_cast<float>(side + 1) * tau / 16;
                        line(at + XY{std::sin(a), std::cos(a)} * radius,
                             at + XY{std::sin(b), std::cos(b)} * radius,
                             layer ? 0xffedf4fbu : 0xa0142030u, (layer ? .28f : .52f) / options_.ribbon_width);
                    }
                }
            }
            return;
        }
        const float center = r * .58f;
        const float halo = std::min(std::max(2.5f, r * .22f), .62f * center);
        const float core = std::min(std::max(1.15f, r * .10f), .48f * halo);
        for (int i = 0; i < 4 && result_.complete(); ++i) {
            const float angle = static_cast<float>(i) * pi * .5f;
            const XY at = {std::sin(angle) * center, std::cos(angle) * center};
            for (int layer = 0; layer < 2; ++layer) {
                const float radius = layer ? core : halo;
                const auto c = layer ? 0xff050505u : 0xffffffffu;
                for (int side = 0; side < 8; ++side) {
                    const float a = static_cast<float>(side) * tau / 8, b = static_cast<float>(side + 1) * tau / 8;
                    tri(at, at + XY{std::sin(a), std::cos(a)} * radius, at + XY{std::sin(b), std::cos(b)} * radius, c, c, c);
                }
            }
        }
    }
    void eye(float r) {
        const XY center{0, -r * .46f};
        const float width = std::clamp(r * .23f, 1.2f, 3.2f);
        const float height = width * .48f;
        for (unsigned layer = 0; layer < 2 && result_.complete(); ++layer) {
            const auto ink = layer ? 0xffedf4fbu : 0xa0142030u;
            const float stroke = (layer ? .28f : .52f) / options_.ribbon_width;
            for (int lid = -1; lid <= 1; lid += 2) {
                for (unsigned i = 0; i < 16 && result_.complete(); ++i) {
                    const float a = static_cast<float>(i) / 16, b = static_cast<float>(i + 1) / 16;
                    const auto point = [&](float t) { return center + XY{(t * 2 - 1) * width,
                        static_cast<float>(lid) * std::sin(t * pi) * height}; };
                    line(point(a), point(b), ink, stroke);
                }
            }
            const float pupil = height * .52f;
            for (unsigned i = 0; i < 16 && result_.complete(); ++i) {
                const float a = tau * static_cast<float>(i) / 16, b = tau * static_cast<float>(i + 1) / 16;
                line(center + XY{std::sin(a), std::cos(a)} * pupil,
                     center + XY{std::sin(b), std::cos(b)} * pupil, ink, stroke);
            }
        }
    }
    void glyphs(float r, std::uint32_t color) {
        if (options_.enhanced_style && (p_.shape == Shape::Rectangle || p_.shape == Shape::Line))
            r = std::min(r, p_.p1 * .5f);
        if (p_.shape == Shape::Target) { line({-r, 0}, {r, 0}, outline(color)); line({0, -r}, {0, r}, outline(color)); }
        int radial = 0;
        if (p_.shape == Shape::Stack) radial = 1;
        else if (p_.shape == Shape::Spread) radial = 2;
        else if (p_.shape == Shape::MoveAway) radial = 3;
        else if (p_.mechanic == Mechanic::Stack || p_.mechanic == Mechanic::Soak) radial = 1;
        else if (p_.mechanic == Mechanic::Spread) radial = 2;
        else if (p_.mechanic == Mechanic::MoveAway || p_.mechanic == Mechanic::Knockback) radial = 3;
        if (p_.flags & Directional) {
            if (radial == 2) pips(r);
            if (radial == 1 || radial == 3) {
                for (int i = 0; i < 4 && result_.complete(); ++i) {
                    const float angle = static_cast<float>(i) * pi * .5f;
                    const XY unit{std::sin(angle), std::cos(angle)};
                    const float outer = r * (options_.enhanced_style ? .83f : 1.10f);
                    const float inner = r * (options_.enhanced_style ? .40f : .26f);
                    arrow(unit * (radial == 1 ? outer : inner), unit * (radial == 1 ? inner : outer),
                          std::max(options_.enhanced_style ? .8f : 2.0f, r * (options_.enhanced_style ? .15f : .20f)));
                }
            }
        }
        if (!options_.enhanced_style && (p_.shape == Shape::MoveAway || p_.mechanic == Mechanic::MoveAway || p_.mechanic == Mechanic::Knockback))
            annulus(r * .70f, r * .74f, 0, tau, sectors(p_, r), opacity(color, .9f), Style::Solid, true, false);
        if (options_.enhanced_style && (p_.shape == Shape::Gaze || p_.mechanic == Mechanic::Gaze)) eye(r);
        if ((p_.flags & Directional) && (p_.shape == Shape::Gaze || p_.mechanic == Mechanic::Gaze))
            arrow({0, options_.enhanced_style ? r * .32f : -r * .20f}, {0, r * .82f}, std::max(1.0f, r * .16f));
        if (p_.mechanic == Mechanic::Soak)
            annulus(r * .28f, r * .34f, 0, tau, sectors(p_, r), opacity(color, .95f), Style::Solid, true, false);
        if (p_.mechanic == Mechanic::Split) {
            line({0, -r * .82f}, {0, r * .82f}, outline(color)); line({-r * .20f, 0}, {r * .20f, 0}, outline(color));
        }
        if (p_.mechanic == Mechanic::Interrupt) {
            line({-r * .48f, -r * .48f}, {r * .48f, r * .48f}, outline(color));
            line({-r * .48f, r * .48f}, {r * .48f, -r * .48f}, outline(color));
        }
        if ((p_.flags & Directional) && p_.shape != Shape::Stack && p_.shape != Shape::Spread &&
            p_.shape != Shape::MoveAway && p_.shape != Shape::Gaze)
            arrow({0, options_.enhanced_style ? r * .32f : 0}, {0, r * (options_.enhanced_style ? .72f : .82f)}, std::max(1.0f, r * .12f));
    }
    void countdown(float r, std::uint32_t color) {
        if (!(p_.flags & Countdown)) return;
        const float fraction = std::clamp(static_cast<float>(distance(marker_.local_impact, now_)) /
            static_cast<float>(distance(marker_.local_impact, marker_.local_start)), 0.0f, 1.0f);
        if (fraction <= 0) return;
        if (options_.enhanced_style) {
            // A small central clock communicates timing without adding another
            // apparent hazard boundary outside the authoritative footprint.
            const int count = 24;
            const float radius = std::clamp(r * .17f, .8f, 2.3f);
            const auto polar = [&](float a) { return XY{std::sin(a) * radius, std::cos(a) * radius}; };
            for (int i = 0; i < count && result_.complete(); ++i) {
                const float a = tau * static_cast<float>(i) / static_cast<float>(count);
                const float b = tau * static_cast<float>(i + 1) / static_cast<float>(count);
                line(polar(a), polar(b), 0xcc142030u, .65f / options_.ribbon_width);
                line(polar(a), polar(b), 0xa06c8196u, .24f / options_.ribbon_width);
            }
            // The final segment ends at the exact impact fraction instead of
            // stepping between coarse sectors. All vertices still sample terrain.
            const float end = tau * fraction;
            for (int i = 0; i < count && result_.complete(); ++i) {
                const float a = tau * static_cast<float>(i) / static_cast<float>(count);
                if (a >= end) break;
                const float b = std::min(end, tau * static_cast<float>(i + 1) / static_cast<float>(count));
                line(polar(a), polar(b), 0xffedf4fbu, .43f / options_.ribbon_width);
            }
            return;
        }
        const int count = sectors(p_, r), visible = std::max(1, static_cast<int>(static_cast<float>(count) * fraction));
        for (int i = 0; i < visible && result_.complete(); ++i) {
            const float a = -pi + tau * static_cast<float>(i) / static_cast<float>(count);
            const float b = -pi + tau * static_cast<float>(i + 1) / static_cast<float>(count);
            line({std::sin(a) * r * 1.035f, std::cos(a) * r * 1.035f},
                 {std::sin(b) * r * 1.035f, std::cos(b) * r * 1.035f}, color | 0xff000000u);
        }
    }

    const RenderMarker& marker_;
    const Packet& p_;
    std::uint32_t now_;
    GeometryQueries& queries_;
    GeometryBudget& budget_;
    const GeometryOptions& options_;
    MarkerGeometry result_;
    bool required_ = false, clamp_ = false;
    XY origin_{}, forward_{}, lateral_{};
    float reference_ = 0, patch_ = 16;
    static constexpr float minor_step = 6, walk_spacing = 2;
    bool drape_seeded_ = false, drape_layered_ = false;
    bool origin_missing_=false;
    XY drape_origin_{};
    bool flat_bounds_=false;
    XY flat_min_{},flat_max_{};
    float flat_z_=0;
    float seed_z_ = 0;
    std::size_t first_work_ = 0, first_floor_query_ = 0;
    std::map<std::pair<std::uint32_t, std::uint32_t>, float> floors_;
    std::map<std::pair<std::uint32_t, std::uint32_t>, float> draped_;
    std::map<std::pair<int, int>, float> drape_grid_;
    std::vector<FaceProof> face_cache_;
};
} // namespace

MarkerGeometry build_geometry(const RenderMarker& marker, std::uint32_t local_now,
                              GeometryQueries& queries, GeometryBudget& budget,
                              const GeometryOptions& options) {
    try { return Builder(marker, local_now, queries, budget, options).run(); }
    catch (const std::bad_alloc&) { return {marker.tag, GeometryStatus::Budget, {}, false}; }
}
} // namespace bastion::telegraphs
