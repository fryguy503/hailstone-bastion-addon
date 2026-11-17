#include "telegraphs_adapter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace bastion::telegraphs {
namespace {
constexpr std::uint32_t required_capabilities = HAILSTONE_WORLD_GEOMETRY | HAILSTONE_WORLD_TERRAIN |
    HAILSTONE_WORLD_DRAWN_ANCHORS | HAILSTONE_WORLD_PRESENT_RECEIPTS | HAILSTONE_WORLD_SURFACE_PATCH;
constexpr std::uint64_t frame_fresh_ms = 1000;
constexpr std::size_t host_maximum_vertices = 98304, host_maximum_batches = 256;

SampleStatus status(int value) {
    if (value == HAILSTONE_WORLD_RESOLVED) return SampleStatus::Resolved;
    if (value == HAILSTONE_WORLD_MISSING) return SampleStatus::Missing;
    return SampleStatus::Deferred;
}
class Queries final : public GeometryQueries {
public:
    Queries(const PluginHostApi& host, const HailstoneWorldFrame& frame) : host_(host), frame_(frame) {}
    FloorSample floor(float x, float y, float probe) override {
        float z = 0;
        // TEL/server and SDK zone-file axes differ. Swap exactly once here.
        const int answer = host_.world_floor_below(host_.host, frame_.frame, frame_.epoch, y, x, probe, &z);
        return {status(answer), z};
    }
    DrawnAnchor drawn_anchor(std::uint32_t id) override {
        HailstoneWorldAnchor anchor{}; anchor.size = sizeof(anchor);
        const auto answer = status(host_.world_anchor(host_.host, frame_.frame, frame_.epoch, id, &anchor));
        if (answer != SampleStatus::Resolved) return {answer};
        if (anchor.size < sizeof(anchor) || anchor.spawn_id != id || !(anchor.flags & HAILSTONE_WORLD_DRAWN_POSE))
            return {SampleStatus::Deferred};
        return {SampleStatus::Resolved, anchor.position[1], anchor.position[0], anchor.position[2], anchor.heading,
                (anchor.flags & HAILSTONE_WORLD_CACHED_FLOOR) != 0, anchor.floor_z};
    }
    SurfacePatchSample surface_patch(const std::array<TriangleVertex, 3>& vertices, float maximum_rise) override {
        HailstoneWorldSurfacePatch query{}; query.size = sizeof(query); query.frame = frame_.frame; query.epoch = frame_.epoch;
        query.maximum_rise = maximum_rise; query.tolerance = .002f;
        for (std::size_t n = 0; n < vertices.size(); ++n) {
            query.vertices[n * 3] = vertices[n].y; query.vertices[n * 3 + 1] = vertices[n].x;
            query.vertices[n * 3 + 2] = vertices[n].z;
        }
        HailstoneWorldSurfacePatchResult result{}; result.size = sizeof(result);
        const auto answer = host_.world_surface_patch(host_.host, &query, &result);
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
            double winding=0;
            for(std::size_t n=0;n<sample.region_count;++n) {
                const auto& point=sample.region[n];const auto& next=sample.region[(n+1)%sample.region_count];const auto& after=sample.region[(n+2)%sample.region_count];
                const double wa=((static_cast<double>(vertices[1].y)-vertices[2].y)*(point[0]-vertices[2].x)+
                    (static_cast<double>(vertices[2].x)-vertices[1].x)*(point[1]-vertices[2].y))/determinant;
                const double wb=((static_cast<double>(vertices[2].y)-vertices[0].y)*(point[0]-vertices[2].x)+
                    (static_cast<double>(vertices[0].x)-vertices[2].x)*(point[1]-vertices[2].y))/determinant;
                if(wa<-.000001||wb<-.000001||wa+wb>1.000001) return {};
                const double turn=(next[0]-point[0])*(after[1]-next[1])-(next[1]-point[1])*(after[0]-next[0]);
                if(turn!=0) {if(winding&&turn*winding<0) return {};winding=turn;}
            }
            if(winding==0) return {};
        }
        return sample;
    }
private:
    const PluginHostApi& host_;
    const HailstoneWorldFrame& frame_;
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
    local_submitted_ = 0;
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
    required_cursor_ = optional_cursor_ = 0;
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
bool Adapter::send(const Bytes& bytes) {
    return selected_ && in_zone_ && available_ && services() &&
        host_->send_packet(host_->host, HAILSTONE_STREAM_ZONE, kOpcode, bytes.data(), bytes.size()) != 0;
}
void Adapter::pulse() {
    preferences_.refresh(host_);
    std::uint64_t now = 0;
    if (!selected_ || !in_zone_ || !clock(now)) return;
    if (!last_frame_clock_ || now < *last_frame_clock_ || now - *last_frame_clock_ > frame_fresh_ms ||
        (host_->world_capabilities(host_->host) & required_capabilities) != required_capabilities) {
        unavailable(); return;
    }
    session_.pulse(static_cast<std::uint32_t>(now), *this);
}
void Adapter::receive(const void* bytes, std::size_t size) {
    std::uint64_t now = 0;
    if ((!bytes && size) || size != kPacketBytes || !selected_ || !in_zone_ || !available_ || !clock(now)) return;
    if (!last_frame_clock_ || now < *last_frame_clock_ || now - *last_frame_clock_ > frame_fresh_ms ||
        (host_->world_capabilities(host_->host) & required_capabilities) != required_capabilities) { unavailable(); return; }
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
        !frame->maximum_batches || frame->maximum_vertices < 3 || !frame->remaining_queries ||
        (now > frame->now_ms ? now - frame->now_ms : frame->now_ms - now) > frame_fresh_ms ||
        !std::isfinite(frame->viewport[2]) || !std::isfinite(frame->viewport[3]) || frame->viewport[2] <= 0 || frame->viewport[3] <= 0) {
        unavailable(); return;
    }
    for (float value : frame->view_projection) if (!std::isfinite(value)) { unavailable(); return; }
    if (frame->epoch == epoch_ && (frame->frame <= last_host_frame_ || (last_frame_clock_ && frame->now_ms < *last_frame_clock_))) return;
    if (frame->epoch != epoch_) {
        unavailable(); epoch_ = frame->epoch;
    }
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
void Adapter::draw(const HailstoneWorldFrame& host_frame, std::uint32_t now) {
    auto frame = session_.ready() ? session_.prepare_frame(now) : RenderFrame{};
    PendingFrame pending{host_frame.frame, frame.frame, host_frame.epoch, {}};
    Queries queries(*host_, host_frame);
    GeometryBudget budget;
    budget.vertex_limit = std::min<std::size_t>(host_frame.maximum_vertices, host_maximum_vertices);
    budget.floor_query_limit = std::min<std::size_t>(host_frame.remaining_queries, GeometryBudget::kMaximumFloorQueries);
    // Stable required-first order, rotated independently within each class.
    // No permanently earlier marker can own the beginning of every frame.
    const auto first_optional = std::stable_partition(frame.markers.begin(), frame.markers.end(), [](const auto& marker) {
        return (marker.packet.flags & RequiredDisplay) != 0;
    });
    const auto required = static_cast<std::size_t>(first_optional - frame.markers.begin());
    const auto optional = frame.markers.size() - required;
    if (required) {
        std::rotate(frame.markers.begin(), frame.markers.begin() + static_cast<std::ptrdiff_t>(required_cursor_ % required), first_optional);
        required_cursor_ = (required_cursor_ + 1) % required;
    }
    if (optional) {
        std::rotate(first_optional, first_optional + static_cast<std::ptrdiff_t>(optional_cursor_ % optional), frame.markers.end());
        optional_cursor_ = (optional_cursor_ + 1) % optional;
    }
    budget.required_markers_remaining = required;
    const auto batch_limit = std::min<std::size_t>(host_frame.maximum_batches, host_maximum_batches);
    for (const auto& marker : frame.markers) {
        if (!preferences_.get("enabled") && !(marker.packet.flags & RequiredDisplay)) continue;
        if (pending.batches.size() >= batch_limit) break;
        auto geometry = build_geometry(marker, now, queries, budget, geometry_options());
        if ((marker.packet.flags & RequiredDisplay) && budget.required_markers_remaining) --budget.required_markers_remaining;
        if (!geometry.complete()) continue;
        std::vector<HailstoneWorldVertex> vertices;
        vertices.reserve(geometry.vertices.size());
        for (const auto& vertex : geometry.vertices) vertices.push_back(sdk_vertex(vertex));
        if (++next_tag_ == 0) ++next_tag_;
        HailstoneWorldSubmission submission{};
        submission.size = sizeof(submission); submission.epoch = host_frame.epoch; submission.frame = host_frame.frame;
        submission.tag = next_tag_; submission.revision = marker.tag.incarnation;
        submission.primitive = HAILSTONE_WORLD_TRIANGLES; submission.vertices = vertices.data(); submission.vertex_count = vertices.size();
        if (!host_->world_submit(host_->host, &submission)) continue;
        pending.batches.emplace(submission.tag, Accepted{geometry.receipt(), submission.revision, static_cast<std::uint32_t>(vertices.size()), false, false});
    }
    if (frame.frame) {
        if (pending_.size() == 8) pending_.pop_front();
        pending_.push_back(std::move(pending));
    }
    draw_local(host_frame, now, queries, budget);
}
void Adapter::world_result(const HailstoneWorldReceipt* receipt) {
    if (!receipt || receipt->size < sizeof(*receipt)) return;
    if (receipt->status == HAILSTONE_WORLD_FRAME_UNAVAILABLE && !receipt->tag) {
        if (receipt->epoch == epoch_ && (!receipt->frame || receipt->frame == last_host_frame_)) unavailable();
        return;
    }
    std::uint64_t now = 0;
    if (!selected_ || !in_zone_ || !available_ || !clock(now) || receipt->epoch != epoch_ ||
        !last_frame_clock_ || now < *last_frame_clock_ || now - *last_frame_clock_ > frame_fresh_ms) return;
    if ((host_->world_capabilities(host_->host) & required_capabilities) != required_capabilities) { unavailable(); return; }
    const auto frame = std::find_if(pending_.begin(), pending_.end(), [&](const auto& pending) {
        return pending.host_frame == receipt->frame && pending.epoch == receipt->epoch;
    });
    if (frame == pending_.end()) return;
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
    if (receipt->status != HAILSTONE_WORLD_FRAME_FINISHED || receipt->tag) return;
    std::vector<MarkerReceipt> markers;
    markers.reserve(frame->batches.size());
    for (const auto& [tag, batch] : frame->batches) { (void)tag; if (batch.received && batch.recorded) markers.push_back(batch.marker); }
    session_.rendered({frame->epoch, frame->session_frame, (receipt->flags & HAILSTONE_WORLD_SUBMITTED) != 0,
                       (receipt->flags & HAILSTONE_WORLD_PRESENTED) != 0, markers}, static_cast<std::uint32_t>(now));
    pending_.erase(frame);
}
} // namespace bastion::telegraphs
