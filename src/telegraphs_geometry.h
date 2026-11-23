#pragma once

#include "telegraphs_state.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

namespace bastion::telegraphs {

enum class SampleStatus { Resolved, Missing, Deferred };
struct FloorSample { SampleStatus status = SampleStatus::Deferred; float z = 0; };
struct DrawnAnchor {
    SampleStatus status = SampleStatus::Deferred;
    float x = 0, y = 0, z = 0, heading = 0;
    // Host-validated current actor floor cache, separate from actual drawn Z.
    // Used only as the layer reference for FollowPosition+ClampToTerrain;
    // output surfaces still require current visual-mesh samples/certificates.
    bool floor_reference_valid = false;
    float floor_z = 0;
};
struct TriangleVertex {
    float x = 0, y = 0, z = 0; // Raw TEL/server axes; adapter swaps XY at SDK boundaries.
    std::uint32_t argb = 0;
};
enum class PatchStatus { Deferred, Planar, Mismatch, Missing };
// The host still certifies coverage of the entire patch. Small height differences
// below the render lift need no separate clipped face.
inline constexpr float kSurfaceProofTolerance = .018f;
struct SurfacePatchSample {
    PatchStatus status = PatchStatus::Deferred;
    float x = 0, y = 0, z = 0;
    bool height_known = false;
    bool face_known = false;
    std::array<TriangleVertex,3> face{};
    std::uint32_t region_count=0;
    std::array<std::array<double,2>,32> region{};
};
class GeometryQueries {
public:
    virtual ~GeometryQueries() = default;
    // Inputs/poses use raw TEL server axes. Only the SDK adapter converts axes.
    // The highest drawn upward visual surface at/below probe_z, including
    // passable visible props. Missing means the query
    // completed and found no surface; unavailable collision/budget is Deferred.
    virtual FloorSample floor(float eq_x, float eq_y, float probe_z) = 0;
    // Only a current actual rendered actor pose may resolve this query,
    // including an explicit host pose for an intentionally hidden first-person
    // body. Logical spawn positions and stale transforms are not substitutes.
    virtual DrawnAnchor drawn_anchor(std::uint32_t id) = 0;
    // A full visual-surface certificate, not a collection of point samples.
    // Planar proves coverage of the whole XY triangle; mismatch/missing supply
    // a strict-interior witness for subdivision. Unavailable proof is Deferred.
    virtual SurfacePatchSample surface_patch(const std::array<TriangleVertex, 3>&, float) { return {}; }
};
enum class GeometryLayer { Complete, Footprint, Countdown };
struct GeometryOptions {
    // Deliberate modern-renderer difference: centered EQ-unit ribbons replace
    // native screen-space lines. The filled hazard silhouette is unchanged.
    float ribbon_width = 0.20f; // Valid range .01 .. 8 EQ units.
    unsigned opacity_percent = 100; // 0 .. 100; RequiredDisplay still clamps A>=128.
    std::uint32_t hostile = 0xa8ff4338u, friendly = 0x9848b5ffu;
    std::uint32_t neutral = 0xa8ffcc33u, safe = 0x9840e69du;
    bool unified_palette = false; // Plugin default on; protocol tests can preserve server-authored colors.
    bool enhanced_style = false; // Fine contrast edges, open glyphs and a compact impact clock.
    // Intentional addon improvement: walk locally connected surfaces, including
    // small props and successive stairs. The raw protocol renderer remains
    // available to tests without changing its reference-layer policy.
    bool bounded_draping = false;
    GeometryLayer layer = GeometryLayer::Complete;
    bool operator==(const GeometryOptions&) const = default;
};
struct GeometryBudget {
    static constexpr std::size_t kMaximumVertices = 393216;
    // Modern host frame allowance, not the owned native renderer's shared
    // 1536-query frame limit. No cross-frame cache warming is assumed here.
    static constexpr std::size_t kMaximumFloorQueries = 262144;
    static constexpr std::size_t kMaximumWork = 1048576;
    std::size_t vertex_limit = kMaximumVertices, floor_query_limit = kMaximumFloorQueries;
    std::size_t work_limit = kMaximumWork;
    std::size_t marker_floor_query_limit = kMaximumFloorQueries, marker_work_limit = kMaximumWork;
    std::size_t vertices_used = 0, floor_queries_used = 0, work_used = 0;
    // Caller maintains this count while walking required-first/frame order.
    // Optional markers cannot consume the final third of vertices or 1024
    // floor probes while any required marker remains to be attempted.
    std::size_t required_markers_remaining = 0;
};
enum class GeometryStatus {
    Complete, NotStarted, Expired, Invalid, MissingAnchor, DeferredAnchor,
    MissingFloor, DeferredFloor, Cliff, Budget, Numerical, NoDecoration
};
struct MarkerPlacement {
    float x = 0, y = 0, z = 0, heading = 0;
    bool operator==(const MarkerPlacement&) const = default;
};
bool follows_anchor(const Packet& packet);
GeometryStatus marker_placement(const Packet& packet, GeometryQueries& queries, MarkerPlacement& placement);

struct MarkerGeometry {
    RenderTag tag;
    GeometryStatus status = GeometryStatus::Invalid;
    // A marker is atomic: failure always returns an empty vector. Completion
    // describes CPU generation, not GPU submission/presentation or readiness.
    std::vector<TriangleVertex> vertices;
    bool terrain_conformed = false;
    bool covers_warning = true;
    bool complete() const { return status == GeometryStatus::Complete; }
    MarkerReceipt receipt() const { return {tag, complete() && covers_warning, terrain_conformed && covers_warning}; }
};

// Host thread only. Budgets are shared across all markers in one frame. Floor
// samples and certificates are reused only inside this invocation; lifts are
// revalidated next frame.
// No renderer capability, Hello, Presented or other wire action is emitted here.
MarkerGeometry build_geometry(const RenderMarker& marker, std::uint32_t local_now,
                              GeometryQueries& queries, GeometryBudget& budget,
                              const GeometryOptions& options = {});

} // namespace bastion::telegraphs
