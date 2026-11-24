#include "telegraphs_adapter.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <span>

namespace bastion::telegraphs {
namespace {
constexpr std::uint32_t required_capabilities = HAILSTONE_WORLD_GEOMETRY | HAILSTONE_WORLD_TERRAIN |
    HAILSTONE_WORLD_DRAWN_ANCHORS | HAILSTONE_WORLD_PRESENT_RECEIPTS | HAILSTONE_WORLD_SURFACE_PATCH;
constexpr std::uint64_t frame_fresh_ms = 1000;
constexpr std::size_t host_maximum_vertices = 98304, host_maximum_batches = 256;
struct MemoBudget {
    std::size_t maximum, per_marker, used = 0;
};

SampleStatus status(int value) {
    if (value == HAILSTONE_WORLD_RESOLVED) return SampleStatus::Resolved;
    if (value == HAILSTONE_WORLD_MISSING) return SampleStatus::Missing;
    return SampleStatus::Deferred;
}
class Queries final : public GeometryQueries {
public:
    Queries(const PluginHostApi& host, const HailstoneWorldFrame& frame)
        : host_(host), frame_(frame), remaining_(frame.remaining_queries) {}
    std::uint32_t remaining() const { return remaining_; }
    void limit_marker(std::uint32_t allowance, TerrainBuildMemo* memo, MemoBudget* memo_budget) {
        marker_remaining_ = std::min(allowance, remaining_);
        memo_ = memo;
        memo_budget_ = memo_budget;
        terrain_bounds_ = memo ? memo->stamp.bounds : std::nullopt;
    }
    const std::optional<std::array<float, 4>>& terrain_bounds() const { return terrain_bounds_; }
    FloorSample floor(float x, float y, float probe) override {
        const std::array key{bits(x), bits(y), bits(probe)};
        if (memo_) {
            const auto saved = memo_->floors.find(key);
            if (saved != memo_->floors.end()) return saved->second;
        }
        if (!spend_one()) return {};
        float z = 0;
        // TEL/server and SDK zone-file axes differ. Swap exactly once here.
        expand_bounds(y, x);
        const int answer = host_.world_floor_below(host_.host, frame_.frame, frame_.epoch, y, x, probe, &z);
        const FloorSample sample{status(answer), z};
        if (sample.status != SampleStatus::Deferred && room_to_memo()) {
            memo_->floors.emplace(key, sample);
            ++memo_budget_->used;
        }
        return sample;
    }
    DrawnAnchor drawn_anchor(std::uint32_t id) override {
        if (const auto saved = anchors_.find(id); saved != anchors_.end()) return saved->second;
        if (!spend_one()) return {};
        HailstoneWorldAnchor anchor{}; anchor.size = sizeof(anchor);
        const auto answer = status(host_.world_anchor(host_.host, frame_.frame, frame_.epoch, id, &anchor));
        DrawnAnchor current{answer};
        if (answer == SampleStatus::Resolved) {
            if (anchor.size < sizeof(anchor) || anchor.spawn_id != id || !(anchor.flags & HAILSTONE_WORLD_DRAWN_POSE)) {
                current.status = SampleStatus::Deferred;
            } else {
                current = {SampleStatus::Resolved, anchor.position[1], anchor.position[0], anchor.position[2], anchor.heading,
                           (anchor.flags & HAILSTONE_WORLD_CACHED_FLOOR) != 0, anchor.floor_z};
            }
        }
        anchors_.emplace(id, current);
        return current;
    }
    SurfacePatchSample surface_patch(const std::array<TriangleVertex, 3>& vertices, float maximum_rise) override {
        std::array<std::uint32_t, 10> key{};
        if (memo_) {
            key[0] = bits(maximum_rise);
            for (std::size_t n = 0; n < vertices.size(); ++n) {
                key[1 + 3 * n] = bits(vertices[n].x);
                key[2 + 3 * n] = bits(vertices[n].y);
                key[3 + 3 * n] = bits(vertices[n].z);
            }
            const auto saved = memo_->patches.find(key);
            if (saved != memo_->patches.end()) {
                return saved->second ? *saved->second : SurfacePatchSample{PatchStatus::Planar};
            }
        }
        auto sample = query_surface_patch(vertices, maximum_rise);
        if (sample.status != PatchStatus::Deferred && room_to_memo()) {
            auto detail = sample.status == PatchStatus::Planar ? nullptr : std::make_unique<SurfacePatchSample>(sample);
            memo_->patches.emplace(key, std::move(detail));
            ++memo_budget_->used;
        }
        return sample;
    }
private:
    std::map<std::uint32_t, DrawnAnchor> anchors_;
    static std::uint32_t bits(float value) { return std::bit_cast<std::uint32_t>(value); }
    bool room_to_memo() const {
        return memo_ && memo_->size() < memo_budget_->per_marker && memo_budget_->used < memo_budget_->maximum;
    }
    SurfacePatchSample query_surface_patch(const std::array<TriangleVertex, 3>& vertices, float maximum_rise) {
        if (!remaining_ || !marker_remaining_) return {};
        HailstoneWorldSurfacePatch query{}; query.size = sizeof(query); query.frame = frame_.frame; query.epoch = frame_.epoch;
        query.maximum_rise = maximum_rise; query.tolerance = kSurfaceProofTolerance;
        for (std::size_t n = 0; n < vertices.size(); ++n) {
            query.vertices[n * 3] = vertices[n].y; query.vertices[n * 3 + 1] = vertices[n].x;
            query.vertices[n * 3 + 2] = vertices[n].z;
            expand_bounds(vertices[n].y, vertices[n].x);
        }
        HailstoneWorldSurfacePatchResult result{}; result.size = sizeof(result);
        int answer = HAILSTONE_WORLD_PATCH_DEFERRED;
#if defined(BASTION_OPTIONAL_WORLD_SURFACE_EXTENSIONS)
        if ((frame_.capabilities & HAILSTONE_WORLD_BOUNDED_SURFACE_PATCH) &&
            HAILSTONE_HOST_HAS(&host_, world_surface_patch_bounded)) {
            const auto allowance = std::min(remaining_, marker_remaining_);
            auto work = allowance;
            answer = host_.world_surface_patch_bounded(host_.host, &query, &result, &work);
            const auto charged = std::min(work, allowance);
            remaining_ -= charged;
            marker_remaining_ -= charged;
            if (work > allowance || (answer != HAILSTONE_WORLD_PATCH_DEFERRED && !work)) return {};
        } else
#endif
        {
            // Older hosts charge internal surface work against their shared frame
            // allowance but cannot expose or limit that work per marker.
            if (!spend_one()) return {};
            answer = host_.world_surface_patch(host_.host, &query, &result);
        }
        if (result.size < sizeof(result)) return {};
        if (answer == HAILSTONE_WORLD_PATCH_PLANAR) return {PatchStatus::Planar};
        const bool known = (result.flags & HAILSTONE_WORLD_PATCH_HEIGHT_KNOWN) != 0;
        SurfacePatchSample sample;
        if (answer == HAILSTONE_WORLD_PATCH_MISMATCH && known) sample.status=PatchStatus::Mismatch;
        else if (answer == HAILSTONE_WORLD_PATCH_MISSING) sample.status=PatchStatus::Missing;
        else return {};
        sample.x=result.witness[1];sample.y=result.witness[0];sample.z=result.witness[2];sample.height_known=known;
        if(result.flags&HAILSTONE_WORLD_PATCH_SOURCE_FACE) {
            if(!known) return {};
            sample.face_known=true;
            for(std::size_t n=0;n<sample.face.size();++n) {
                for(std::size_t axis=0;axis<3;++axis) if(!std::isfinite(result.source_face[n*3+axis])) return {};
                sample.face[n]={result.source_face[n*3+1],result.source_face[n*3],result.source_face[n*3+2],0};
            }
        }
        if(result.flags&HAILSTONE_WORLD_PATCH_REGION) {
            if(!known||!sample.face_known||result.region_count<3||result.region_count>sample.region.size()) return {};
            sample.region_count=result.region_count;
            for(std::size_t n=0;n<sample.region_count;++n) {
                const double x=result.region_xy[n*2+1],y=result.region_xy[n*2];
                if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>2000000||std::abs(y)>2000000) return {};
                sample.region[n]={x,y};
            }
            const double determinant=(static_cast<double>(vertices[1].y)-vertices[2].y)*(vertices[0].x-vertices[2].x)+
                (static_cast<double>(vertices[2].x)-vertices[1].x)*(vertices[0].y-vertices[2].y);
            if(determinant==0) return {};
            double twice_area = 0;
            const auto& origin = sample.region.front();
            for(std::size_t n=0;n<sample.region_count;++n) {
                const auto& point=sample.region[n];const auto& next=sample.region[(n+1)%sample.region_count];
                const double wa=((static_cast<double>(vertices[1].y)-vertices[2].y)*(point[0]-vertices[2].x)+
                    (static_cast<double>(vertices[2].x)-vertices[1].x)*(point[1]-vertices[2].y))/determinant;
                const double wb=((static_cast<double>(vertices[2].y)-vertices[0].y)*(point[0]-vertices[2].x)+
                    (static_cast<double>(vertices[0].x)-vertices[2].x)*(point[1]-vertices[2].y))/determinant;
                if(wa<-.000001||wb<-.000001||wa+wb>1.000001) return {};
                twice_area += (point[0] - origin[0]) * (next[1] - origin[1]) -
                              (point[1] - origin[1]) * (next[0] - origin[0]);
            }
            if (!std::isfinite(twice_area) || twice_area == 0) return {};
            // Clipping can introduce a near-collinear point with the opposite
            // sign from floating roundoff. Apply the host's convexity tolerance
            // while rejecting a real concave turn or degenerate region.
            const double direction = twice_area > 0 ? 1 : -1;
            for (std::size_t n = 0; n < sample.region_count; ++n) {
                const auto& point = sample.region[n];
                const auto& next = sample.region[(n + 1) % sample.region_count];
                const auto& after = sample.region[(n + 2) % sample.region_count];
                const double left = (next[0] - point[0]) * (after[1] - next[1]);
                const double right = (next[1] - point[1]) * (after[0] - next[0]);
                const double tolerance = 1.0e-12 * std::max(1.0, std::abs(left) + std::abs(right));
                if (direction * (left - right) < -tolerance) return {};
            }
        }
        return sample;
    }
    void expand_bounds(float x, float y) {
        if (!terrain_bounds_) terrain_bounds_ = std::array{x, y, x, y};
        else {
            auto& [min_x, min_y, max_x, max_y] = *terrain_bounds_;
            min_x = std::min(x, min_x); min_y = std::min(y, min_y);
            max_x = std::max(x, max_x); max_y = std::max(y, max_y);
        }
    }
    bool spend_one() {
        if (!remaining_ || !marker_remaining_) return false;
        --remaining_;
        --marker_remaining_;
        return true;
    }
    const PluginHostApi& host_;
    const HailstoneWorldFrame& frame_;
    std::uint32_t remaining_ = 0, marker_remaining_ = 0;
    TerrainBuildMemo* memo_ = nullptr;
    MemoBudget* memo_budget_ = nullptr;
    std::optional<std::array<float, 4>> terrain_bounds_;
};
HailstoneWorldVertex sdk_vertex(const TriangleVertex& vertex) {
    HailstoneWorldVertex out{};
    out.position[0] = vertex.y; out.position[1] = vertex.x; out.position[2] = vertex.z;
    out.color[0] = static_cast<float>((vertex.argb >> 16) & 255) / 255;
    out.color[1] = static_cast<float>((vertex.argb >> 8) & 255) / 255;
    out.color[2] = static_cast<float>(vertex.argb & 255) / 255;
    out.color[3] = static_cast<float>(vertex.argb >> 24) / 255;
    return out;
}
std::vector<HailstoneWorldVertex> sdk_vertices(const MarkerGeometry& geometry) {
    std::vector<HailstoneWorldVertex> vertices;
    vertices.reserve(geometry.vertices.size());
    for (const auto& vertex : geometry.vertices) vertices.push_back(sdk_vertex(vertex));
    return vertices;
}
} // namespace

void Adapter::set_host(const PluginHostApi* host) {
    if (HAILSTONE_HOST_HAS(host_, remove_ui_function)) host_->remove_ui_function(host_->host, "telegraphs");
    peer(false);
    host_ = host;
    last_clock_.reset(); last_frame_clock_.reset(); retired_zone_epoch_.reset(); context_retired_epoch_.reset();
    last_host_frame_ = 0; epoch_ = 0; in_zone_ = false;
}
void Adapter::unavailable() {
    available_ = false;
    character_scene_.reset();
    fallbacks_.clear();
    local_submitted_ = 0;
    footprints_.clear();
    terrain_builds_.clear();
    countdown_terrain_.clear();
    reused_footprints_ = deferred_warning_footprints_ = 0;
    pending_.clear();
    session_.renderer({});
}
void Adapter::peer(bool selected) {
    preferences_.invalidate_context();
    local_.clear(); local_submitted_ = 0;
    unavailable();
    session_.context(false, false, 0);
    selected_ = selected;
    epoch_ = 0; last_host_frame_ = 0; last_frame_clock_.reset(); retired_zone_epoch_.reset(); context_retired_epoch_.reset();
    session_.context(selected_, in_zone_, epoch_);
}
void Adapter::game_state(int state) {
    preferences_.invalidate_context();
    in_zone_ = state == GAMESTATE_INGAME;
    if (!in_zone_) {
        local_.clear(); local_submitted_ = 0;
        if (epoch_) context_retired_epoch_ = epoch_;
        unavailable();
        session_.context(selected_, false, epoch_);
    }
}
void Adapter::begin_zone() {
    preferences_.invalidate_context();
    local_.clear(); local_submitted_ = 0;
    unavailable();
    if (epoch_) retired_zone_epoch_ = epoch_;
    session_.context(selected_, false, 0);
    epoch_ = 0; last_host_frame_ = 0; last_frame_clock_.reset();
}
bool Adapter::services() const {
    return HAILSTONE_HOST_HAS(host_, world_capabilities) && HAILSTONE_HOST_HAS(host_, world_floor_below) &&
        HAILSTONE_HOST_HAS(host_, world_surface_patch) &&
        HAILSTONE_HOST_HAS(host_, world_anchor) && HAILSTONE_HOST_HAS(host_, world_submit) &&
        HAILSTONE_HOST_HAS(host_, monotonic_ms) && HAILSTONE_HOST_HAS(host_, send_packet);
}
bool Adapter::clock(std::uint64_t& now) {
    if (!services()) { unavailable(); return false; }
    now = host_->monotonic_ms(host_->host);
    if (last_clock_ && now < *last_clock_) { unavailable(); last_clock_ = now; return false; }
    last_clock_ = now;
    return true;
}
bool Adapter::scene_context() {
    constexpr auto permissions = HAILSTONE_CAP_READ | HAILSTONE_CAP_UI | HAILSTONE_CAP_EXTEND | HAILSTONE_CAP_PACKETS;
    if (HAILSTONE_HOST_HAS(host_, caps) && (host_->caps(host_->host) & permissions) != permissions) {
        unavailable();
        return false;
    }
    if (!HAILSTONE_HOST_HAS(host_, get_player)) return true;
    HailstonePlayer player{};
    player.size = sizeof(player);
    if (!host_->get_player(host_->host, &player) || !player.in_zone || !player.spawn_id) {
        unavailable();
        return false;
    }
    const CharacterScene current{player.spawn_id, player.zone_id, player.zone_instance};
    if (character_scene_ && *character_scene_ != current) {
        unavailable();
        return false;
    }
    character_scene_ = current;
    return true;
}
bool Adapter::send(const Bytes& bytes) {
    return selected_ && in_zone_ && available_ && services() &&
        host_->send_packet(host_->host, HAILSTONE_STREAM_ZONE, kOpcode, bytes.data(), bytes.size()) != 0;
}
void Adapter::pulse() {
    preferences_.refresh(host_);
    std::uint64_t now = 0;
    if (!selected_ || !in_zone_ || !available_ || !clock(now) || !scene_context()) return;
    // A skipped draw leaves the same zone's transport live. Expire warnings
    // and maintain the lease while waiting for fresh presentation receipts.
    session_.pulse(static_cast<std::uint32_t>(now), *this);
}
void Adapter::receive(const void* bytes, std::size_t size) {
    std::uint64_t now = 0;
    if (!bytes || size != kPacketBytes || !selected_ || !in_zone_ || !available_ ||
        !clock(now) || !scene_context()) return;
    session_.receive({static_cast<const std::byte*>(bytes), size}, static_cast<std::uint32_t>(now), epoch_);
}
void Adapter::world_frame(const HailstoneWorldFrame* frame) {
    std::uint64_t now = 0;
    if (!selected_ || !in_zone_ || !frame || frame->size < sizeof(*frame) || !frame->frame || !frame->epoch ||
        !clock(now)) { unavailable(); return; }
    if ((retired_zone_epoch_ && !newer(frame->epoch, *retired_zone_epoch_)) ||
        (epoch_ && frame->epoch != epoch_ && !newer(frame->epoch, epoch_))) return;
    if ((frame->capabilities & required_capabilities) != required_capabilities ||
        (host_->world_capabilities(host_->host) & required_capabilities) != required_capabilities ||
        !frame->maximum_batches || frame->maximum_vertices < 3 ||
        (now > frame->now_ms ? now - frame->now_ms : frame->now_ms - now) > frame_fresh_ms ||
        !std::isfinite(frame->viewport[2]) || !std::isfinite(frame->viewport[3]) || frame->viewport[2] <= 0 || frame->viewport[3] <= 0) {
        unavailable(); return;
    }
    for (float value : frame->view_projection) if (!std::isfinite(value)) { unavailable(); return; }
    if (frame->epoch == epoch_ && (frame->frame <= last_host_frame_ || (last_frame_clock_ && frame->now_ms < *last_frame_clock_))) return;
    if (frame->epoch != epoch_) {
        unavailable(); epoch_ = frame->epoch;
    }
    if (!scene_context()) return;
    session_.context(selected_, true, epoch_);
    retired_zone_epoch_.reset();
    last_host_frame_ = frame->frame; last_frame_clock_ = frame->now_ms;
    available_ = true;
    // This reports the implemented CPU contract only while all generic services
    // attest a usable acquired scene. Actual proof still requires receipts.
    session_.renderer({kVersion, kAllShapes, kKnownFlags, true, true, true, true});
    // context(false/true) resets protocol state, unlike a renderer-only loss.
    // A surviving same-epoch server lease still needs an explicit replacement.
    if (context_retired_epoch_ && *context_retired_epoch_ == epoch_) session_.request_snapshot();
    context_retired_epoch_.reset();
    session_.pulse(static_cast<std::uint32_t>(now), *this);
    draw(*frame, static_cast<std::uint32_t>(now));
}
std::uint64_t Adapter::submit(const HailstoneWorldFrame& frame, std::uint64_t revision,
                              std::span<const HailstoneWorldVertex> vertices) {
    if (++next_tag_ == 0) ++next_tag_;
    HailstoneWorldSubmission submission{};
    submission.size = sizeof(submission); submission.epoch = frame.epoch; submission.frame = frame.frame;
    submission.tag = next_tag_; submission.revision = revision;
    submission.primitive = HAILSTONE_WORLD_TRIANGLES;
    submission.vertices = vertices.data(); submission.vertex_count = vertices.size();
    return host_->world_submit(host_->host, &submission) ? submission.tag : 0;
}
void Adapter::draw(const HailstoneWorldFrame& host_frame, std::uint32_t now) {
    const auto frame = session_.ready() ? session_.prepare_frame(now) : RenderFrame{};
    PendingFrame pending{host_frame.frame, frame.frame, host_frame.epoch, {}};
    Queries queries(*host_, host_frame);
    GeometryBudget budget;
    budget.vertex_limit = std::min<std::size_t>(host_frame.maximum_vertices, host_maximum_vertices);
    const auto batch_limit = std::min<std::size_t>(host_frame.maximum_batches, host_maximum_batches);
    auto options = geometry_options();
    options.layer = GeometryLayer::Footprint;
    if (cached_options_ != options) {
        footprints_.clear();
        terrain_builds_.clear();
        countdown_terrain_.clear();
    }
    cached_options_ = options;
    reused_footprints_ = deferred_warning_footprints_ = 0;
    fallbacks_.clear();

    std::vector<const RenderMarker*> required, optional, countdowns;
    std::map<std::uint64_t, MarkerPlacement> poses;
    std::optional<std::array<float, 2>> player_position;
    bool queried_player = false;
    const auto show_fallback = [&](const RenderMarker& marker, std::string_view reason) {
        if (reached(now, marker.local_impact)) return;
        FallbackWarning warning{marker, reason, std::nullopt, {}};
        const auto& packet = marker.packet;
        const auto pose = poses.find(marker.tag.incarnation);
        if (!follows_anchor(packet) || !(packet.flags & FollowPosition) || pose != poses.end()) {
            const auto placement = pose == poses.end() ? MarkerPlacement{packet.x, packet.y, packet.z, packet.heading} : pose->second;
            // The camera/view-projection service uses zone-file XY; TEL uses the reverse.
            const auto x = placement.y, y = placement.x, z = placement.z;
            if (!queried_player) {
                queried_player = true;
                HailstonePlayer player{}; player.size = sizeof(player);
                HailstoneSpawn spawn{}; spawn.size = sizeof(spawn);
                if (HAILSTONE_HOST_HAS(host_, get_player) && HAILSTONE_HOST_HAS(host_, get_spawn) &&
                    host_->get_player(host_->host, &player) && player.in_zone &&
                    host_->get_spawn(host_->host, player.spawn_id, &spawn) && std::isfinite(spawn.x) && std::isfinite(spawn.y)) {
                    player_position = std::array{spawn.x, spawn.y};
                }
            }
            if (player_position) warning.distance = std::hypot(x - (*player_position)[0], y - (*player_position)[1]);
            const auto& m = host_frame.view_projection;
            const float clip_x = m[0] * x + m[4] * y + m[8] * z + m[12];
            const float clip_w = m[3] * x + m[7] * y + m[11] * z + m[15];
            if (std::isfinite(clip_x) && std::isfinite(clip_w)) {
                if (clip_w < 0) warning.direction = "behind";
                else if (clip_w > 0 && std::abs(clip_x) <= .15f * clip_w) warning.direction = "ahead";
                else if (clip_w > 0) warning.direction = clip_x < 0 ? "left" : "right";
            }
        }
        if (packet.flags & RequiredDisplay) ++deferred_warning_footprints_;
        fallbacks_.push_back(std::move(warning));
    };
    // Resolve each live anchor once in this acquired frame. A previous pose
    // cannot authorize an old footprint when the actor leaves or moves.
    queries.limit_marker(queries.remaining(), nullptr, nullptr);
    std::set<std::uint64_t> active, active_clocks;
    for (const auto& marker : frame.markers) {
        const bool warning = (marker.packet.flags & RequiredDisplay) != 0;
        if (!warning && !preferences_.get("enabled")) continue;
        if (follows_anchor(marker.packet)) {
            MarkerPlacement placement;
            if (marker_placement(marker.packet, queries, placement) != GeometryStatus::Complete) {
                show_fallback(marker, "missing-anchor");
                continue;
            }
            poses.emplace(marker.tag.incarnation, placement);
        }
        active.insert(marker.tag.incarnation);
        if ((marker.packet.flags & Countdown) && !reached(now, marker.local_impact)) active_clocks.insert(marker.tag.incarnation);
        (warning ? required : optional).push_back(&marker);
    }
    const auto regional_revision = [&](const std::array<float, 4>& bounds) -> std::optional<std::uint32_t> {
#if defined(BASTION_OPTIONAL_WORLD_SURFACE_EXTENSIONS)
        if (!(host_frame.capabilities & HAILSTONE_WORLD_SURFACE_CACHE_REVISION) ||
            !HAILSTONE_HOST_HAS(host_, world_surface_cache_revision)) return std::nullopt;
        const auto [min_x, min_y, max_x, max_y] = bounds;
        std::uint32_t revision = 0;
        const auto answer = host_->world_surface_cache_revision(host_->host, host_frame.frame, host_frame.epoch,
                                                               min_x, min_y, max_x, max_y, &revision);
        if (answer != HAILSTONE_WORLD_RESOLVED) return std::nullopt;
        return revision;
#else
        (void)bounds;
        return std::nullopt;
#endif
    };
    const auto terrain_current = [&](const TerrainStamp& stamp, std::uint64_t incarnation) {
        const auto pose = poses.find(incarnation);
        if (stamp.pose && (pose == poses.end() || pose->second != *stamp.pose)) return false;
        if (host_frame.collision_revision && stamp.collision_revision == host_frame.collision_revision) return true;
        return stamp.regional_revision && stamp.bounds && regional_revision(*stamp.bounds) == stamp.regional_revision;
    };
    const auto terrain_stamp = [&](const RenderMarker& marker) {
        TerrainStamp stamp{host_frame.collision_revision, queries.terrain_bounds(), std::nullopt, std::nullopt};
        if (stamp.bounds) stamp.regional_revision = regional_revision(*stamp.bounds);
        if (const auto pose = poses.find(marker.tag.incarnation); pose != poses.end()) stamp.pose = pose->second;
        return stamp;
    };
    std::size_t cached_vertices = 0;
    std::erase_if(footprints_, [&](const auto& entry) {
        if (!active.contains(entry.first) || !terrain_current(entry.second.terrain, entry.first)) return true;
        cached_vertices += entry.second.vertices.size();
        return false;
    });
    MemoBudget footprint_memo_budget{65536, 32768};
    MemoBudget countdown_memo_budget{8192, 2048};
    const auto prune_memos = [&](auto& memos, const auto& active_keys, MemoBudget& memo_budget) {
        std::erase_if(memos, [&](const auto& entry) {
            if (!active_keys.contains(entry.first) || !terrain_current(entry.second.stamp, entry.first)) return true;
            memo_budget.used += entry.second.size();
            return false;
        });
    };
    prune_memos(terrain_builds_, active, footprint_memo_budget);
    prune_memos(countdown_terrain_, active_clocks, countdown_memo_budget);

    std::size_t batches = 0;
    const auto draw_footprint = [&](const RenderMarker& marker, const Footprint& footprint) {
        if (batches >= batch_limit || footprint.vertices.size() > budget.vertex_limit - budget.vertices_used) return false;
        std::span<const HailstoneWorldVertex> vertices = footprint.vertices;
        if (marker.packet.flags & Pulse) {
            const auto remaining = static_cast<std::uint32_t>(std::max(0, distance(marker.local_impact, now)));
            const std::uint32_t period = options.enhanced_style && remaining <= 2000 ? 500u : 900u;
            const auto phase = options.enhanced_style ? remaining : now;
            const float opacity = .88f + .12f * std::sin(2 * std::numbers::pi_v<float> *
                static_cast<float>(phase % period) / static_cast<float>(period));
            const float alpha_floor = (marker.packet.flags & RequiredDisplay) ? 128.0f / 255.0f : 0.0f;
            // Animate only the submitted colors. The certified vertices stay
            // constant and their positions never need another terrain proof.
            pulse_vertices_.assign(vertices.begin(), vertices.end());
            for (auto& vertex : pulse_vertices_) vertex.color[3] = std::max(alpha_floor, vertex.color[3] * opacity);
            vertices = pulse_vertices_;
        }
        const auto tag = submit(host_frame, marker.tag.incarnation, vertices);
        if (!tag) return false;
        ++batches;
        budget.vertices_used += footprint.vertices.size();
        if (marker.packet.flags & RequiredDisplay) {
            pending.batches.emplace(tag, Accepted{footprint.receipt, marker.tag.incarnation,
                static_cast<std::uint32_t>(footprint.vertices.size()), false, false});
        }
        if ((marker.packet.flags & Countdown) && !reached(now, marker.local_impact)) countdowns.push_back(&marker);
        return true;
    };
    const auto limit_work = [&](std::size_t jobs, TerrainBuildMemo* memo = nullptr, MemoBudget* memo_budget = nullptr) {
        const auto share = queries.remaining() / static_cast<std::uint32_t>(jobs);
        queries.limit_marker(std::max(1u, share), memo, memo_budget);
        // The builder also bounds CPU replay work. Cached results count toward
        // this independent limit but do not consume native frame queries.
        budget.marker_floor_query_limit = std::max<std::size_t>(1, (budget.floor_query_limit - budget.floor_queries_used) / jobs);
        budget.marker_work_limit = std::max<std::size_t>(1, (budget.work_limit - budget.work_used) / jobs);
    };
    const auto draw_footprints = [&](const std::vector<const RenderMarker*>& markers, std::size_t& start) {
        std::vector<const RenderMarker*> fresh;
        for (const auto* marker : markers) {
            const auto cached = footprints_.find(marker->tag.incarnation);
            if (cached == footprints_.end()) fresh.push_back(marker);
            else if (draw_footprint(*marker, cached->second)) ++reused_footprints_;
            else show_fallback(*marker, "submission");
        }
        // Older hosts cannot report what a complex terrain patch spent. If
        // following warnings keep rebuilding, rotate fresh work so one moving
        // actor cannot consume the native frame's entire allowance every time.
        if (fresh.size() > 1 && std::any_of(fresh.begin(), fresh.end(), [](const auto* marker) {
            return follows_anchor(marker->packet);
        })) {
            start %= fresh.size();
            std::rotate(fresh.begin(), fresh.begin() + static_cast<std::ptrdiff_t>(start), fresh.end());
            ++start;
        }
        for (std::size_t i = 0; i < fresh.size(); ++i) {
            const auto& marker = *fresh[i];
            if (batches >= batch_limit || budget.vertex_limit - budget.vertices_used < 3) {
                for (std::size_t j = i; j < fresh.size(); ++j) show_fallback(*fresh[j], "budget");
                break;
            }
            // Reserve a share of the remaining host work for later candidates.
            // Preserve charges from failed geometry, but account vertices only
            // after the host accepts a complete submission.
            TerrainBuildMemo* memo = nullptr;
            if (marker.packet.flags & ClampToTerrain) {
                memo = &terrain_builds_.try_emplace(marker.tag.incarnation).first->second;
            }
            limit_work(fresh.size() - i, memo, &footprint_memo_budget);
            const auto already_drawn = budget.vertices_used;
            auto geometry = build_geometry(marker, now, queries, budget, options);
            budget.vertices_used = already_drawn;
            auto stamp = terrain_stamp(marker);
            if (memo) {
                // Retry only terrain/work deferrals. Permanent geometry errors
                // cannot be healed by collecting the same proof again.
                if (memo->size() && (stamp.collision_revision || stamp.regional_revision) &&
                    (geometry.status == GeometryStatus::DeferredFloor || geometry.status == GeometryStatus::Budget)) {
                    memo->stamp = stamp;
                } else {
                    footprint_memo_budget.used -= memo->size();
                    terrain_builds_.erase(marker.tag.incarnation);
                }
            }
            if (!geometry.complete()) {
                show_fallback(marker, geometry.status == GeometryStatus::Budget ? "budget" : "terrain");
                continue;
            }
            Footprint footprint{sdk_vertices(geometry), geometry.receipt(), std::move(stamp)};
            if (!draw_footprint(marker, footprint)) {
                show_fallback(marker, "submission");
                continue;
            }
            if (footprint.vertices.size() > host_maximum_vertices - cached_vertices) continue;
            if (footprint.terrain.collision_revision || footprint.terrain.regional_revision) {
                cached_vertices += footprint.vertices.size();
                footprints_.emplace(marker.tag.incarnation, std::move(footprint));
            }
        }
    };
    // A failed clock cannot withdraw an already-submitted warning or qualify it
    // for presentation. Clocks use only the work and draw space still available.
    const auto draw_countdowns = [&] {
        auto clock_options = options;
        clock_options.layer = GeometryLayer::Countdown;
        for (std::size_t i = 0; i < countdowns.size() && batches < batch_limit; ++i) {
            if (budget.vertex_limit - budget.vertices_used < 3) break;
            const auto& marker = *countdowns[i];
            TerrainBuildMemo* memo = nullptr;
            if (marker.packet.flags & ClampToTerrain) {
                memo = &countdown_terrain_.try_emplace(marker.tag.incarnation).first->second;
            }
            limit_work(countdowns.size() - i, memo, &countdown_memo_budget);
            const auto already_drawn = budget.vertices_used;
            auto clock = build_geometry(marker, now, queries, budget, clock_options);
            if (memo) {
                auto stamp = terrain_stamp(marker);
                // Capture the static clock ring and complete arc segments.
                // The exact moving tip still asks for current terrain proof.
                if (memo->size() && (stamp.collision_revision || stamp.regional_revision)) memo->stamp = std::move(stamp);
                else {
                    countdown_memo_budget.used -= memo->size();
                    countdown_terrain_.erase(marker.tag.incarnation);
                }
            }
            if (clock.complete() && submit(host_frame, marker.tag.incarnation, sdk_vertices(clock))) ++batches;
            else budget.vertices_used = already_drawn;
        }
        countdowns.clear();
    };
    draw_footprints(required, fresh_actor_order_[0]);
    draw_countdowns();
    draw_footprints(optional, fresh_actor_order_[1]);
    draw_countdowns();
    if (frame.frame) {
        if (pending_.size() == 8) pending_.pop_front();
        pending_.push_back(std::move(pending));
    }
    limit_work(1);
    draw_local(host_frame, now, queries, budget, batch_limit - batches);
}
void Adapter::world_result(const HailstoneWorldReceipt* receipt) {
    if (!receipt || receipt->size < sizeof(*receipt)) return;
    if (receipt->status == HAILSTONE_WORLD_FRAME_UNAVAILABLE && !receipt->tag) {
        if (receipt->epoch == epoch_) {
            // Released hosts use this receipt for an intentional pacing skip.
            // Retire render tickets while keeping the same zone's live packets.
            std::erase_if(pending_, [&](const auto& pending) {
                return !receipt->frame || pending.host_frame == receipt->frame;
            });
        }
        return;
    }
    std::uint64_t now = 0;
    if (!selected_ || !in_zone_ || !available_ || !clock(now) || receipt->epoch != epoch_ ||
        !last_frame_clock_ || now < *last_frame_clock_ || now - *last_frame_clock_ > frame_fresh_ms) return;
    const auto frame = std::find_if(pending_.begin(), pending_.end(), [&](const auto& pending) {
        return pending.host_frame == receipt->frame && pending.epoch == receipt->epoch;
    });
    if (frame == pending_.end()) return;
    if ((host_->world_capabilities(host_->host) & required_capabilities) != required_capabilities) {
        pending_.erase(frame);
        return;
    }
    if (receipt->status == HAILSTONE_WORLD_BATCH_FINISHED && receipt->tag) {
        const auto batch = frame->batches.find(receipt->tag);
        if (batch == frame->batches.end()) return;
        auto& accepted = batch->second;
        if (accepted.received) return;
        accepted.received = true;
        accepted.recorded = receipt->revision == accepted.revision && receipt->vertex_count == accepted.vertex_count &&
            (receipt->flags & HAILSTONE_WORLD_RECORDED) != 0;
        return;
    }
    if (receipt->status != HAILSTONE_WORLD_FRAME_FINISHED || receipt->tag || !scene_context()) return;
    std::vector<MarkerReceipt> markers;
    markers.reserve(frame->batches.size());
    for (const auto& [tag, batch] : frame->batches) { (void)tag; if (batch.received && batch.recorded) markers.push_back(batch.marker); }
    session_.rendered({frame->epoch, frame->session_frame, (receipt->flags & HAILSTONE_WORLD_SUBMITTED) != 0,
                       (receipt->flags & HAILSTONE_WORLD_PRESENTED) != 0, markers}, static_cast<std::uint32_t>(now));
    pending_.erase(frame);
}
} // namespace bastion::telegraphs
