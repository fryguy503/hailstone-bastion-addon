#include "telegraphs_state.h"

#include <algorithm>

namespace bastion::telegraphs {

bool RendererCapabilities::complete_v3() const noexcept {
    return protocol_version == kVersion && (shapes & kAllShapes) == kAllShapes &&
           (flags & kKnownFlags) == kKnownFlags && resolves_anchors && styles_and_mechanics &&
           per_vertex_terrain && presentation_receipts;
}

void Session::clear_proofs() {
    acknowledgements_.clear();
    issued_ = {};
    frame_cursor_ = 0;
    for (auto& [id, entry] : live_) { (void)id; entry.acknowledged = false; }
}
void Session::clear_snapshot() {
    staged_.clear(); snapshot_generation_.reset(); expected_ = 0; snapshot_started_ = 0;
}
void Session::reset_protocol() {
    live_.clear(); retired_.clear(); retired_order_.clear(); generation_.reset();
    clear_snapshot(); clear_proofs();
    negotiated_ = false; recovering_ = false; snapshot_requested_ = false;
    offset_.reset(); next_hello_.reset(); hellos_ = {}; hello_cursor_ = 0;
    ack_window_count_ = 0;
}
void Session::context(bool selected_peer, bool in_zone, std::uint32_t epoch) {
    if (selected_peer != selected_ || in_zone != in_zone_ || epoch != epoch_) reset_protocol();
    selected_ = selected_peer; in_zone_ = in_zone; epoch_ = epoch;
}
void Session::renderer(RendererCapabilities capabilities) {
    const bool was_capable = capabilities_.complete_v3();
    const bool needs_replacement = negotiated_ || recovering_ || snapshot_requested_ || generation_.has_value();
    capabilities_ = capabilities;
    if (was_capable != capabilities_.complete_v3()) {
        reset_protocol();
        // A fresh server readiness lease survives a local device interruption.
        // Its normal HelloAck need not include a snapshot, so explicitly ask
        // for replacement data after abandoning a negotiated renderer state.
        if (needs_replacement) request_snapshot();
    }
}
void Session::request_snapshot() {
    recovering_ = snapshot_requested_ = true;
    clear_proofs(); next_hello_.reset();
}
void Session::begin_generation(std::uint32_t value) {
    live_.clear(); retired_.clear(); retired_order_.clear(); generation_ = value;
    clear_snapshot(); clear_proofs();
}
void Session::retire(std::uint32_t id, std::uint32_t revision, bool restart) {
    if (auto found = retired_.find(id); found != retired_.end()) {
        if (newer(revision, found->second.revision)) found->second = {revision, restart};
        else if (revision == found->second.revision && !restart) found->second.update_may_restart = false;
        return;
    }
    if (retired_.size() == kMaximumActive * 2) {
        retired_.erase(retired_order_.front()); retired_order_.pop_front();
    }
    retired_.emplace(id, Retired{revision, restart}); retired_order_.push_back(id);
}
void Session::expire(std::uint32_t now) {
    for (auto it = live_.begin(); it != live_.end();) {
        if (reached(now, it->second.marker.local_expire)) {
            retire(it->first, it->second.marker.packet.revision, true);
            it = live_.erase(it);
        } else ++it;
    }
    std::erase_if(acknowledgements_, [&](const PendingAck& ack) { return !current(ack.tag, now, true); });
}
Session::Entry Session::make_entry(const Packet& packet, std::uint32_t now) {
    const auto server_now = offset_ ? now + *offset_ : packet.server_now_ms;
    RenderMarker marker;
    marker.packet = packet;
    marker.tag = {epoch_, packet.generation, packet.id, packet.revision, ++incarnation_};
    marker.local_start = now + (packet.start_ms - server_now);
    marker.local_impact = now + (packet.impact_ms - server_now);
    marker.local_expire = now + (packet.expire_ms - server_now);
    return {marker, false};
}

Result Session::apply(const Packet& p, std::uint32_t now) {
    if (p.action == Action::SnapshotBegin) {
        if ((generation_ && p.generation != *generation_ && !newer(p.generation, *generation_)) ||
            (snapshot_generation_ && p.generation != *snapshot_generation_ && !newer(p.generation, *snapshot_generation_)))
            return Result::StaleGeneration;
        clear_snapshot(); snapshot_generation_ = p.generation; expected_ = p.revision; snapshot_started_ = now;
        snapshot_requested_ = false;
        clear_proofs(); // No old live render completion can qualify a replacement snapshot.
        return Result::Applied;
    }
    if (p.action == Action::SnapshotEnd) {
        if (!snapshot_generation_ || p.generation != *snapshot_generation_) return Result::StaleGeneration;
        if (staged_.size() != expected_ || (p.revision && p.revision != expected_)) {
            clear_snapshot(); request_snapshot(); return Result::SnapshotIncomplete;
        }
        live_ = std::move(staged_); generation_ = *snapshot_generation_;
        retired_.clear(); retired_order_.clear(); clear_snapshot(); clear_proofs();
        recovering_ = snapshot_requested_ = false;
        expire(now);
        return Result::Applied;
    }
    if (snapshot_generation_ && p.action == Action::Create && p.generation == *snapshot_generation_) {
        const auto existing = staged_.find(p.id);
        if (existing != staged_.end()) {
            if (p.revision == existing->second.marker.packet.revision) return Result::Duplicate;
            if (!newer(p.revision, existing->second.marker.packet.revision)) return Result::StaleRevision;
        } else if (staged_.size() >= expected_ || staged_.size() == kMaximumActive) {
            clear_snapshot(); request_snapshot(); return Result::Capacity;
        }
        staged_.insert_or_assign(p.id, make_entry(p, now));
        return Result::Applied;
    }
    if (!generation_) begin_generation(p.generation);
    else if (p.generation != *generation_) {
        if (!newer(p.generation, *generation_)) return Result::StaleGeneration;
        begin_generation(p.generation);
    }
    if (p.action == Action::ClearAll) {
        live_.clear(); retired_.clear(); retired_order_.clear(); clear_snapshot(); clear_proofs();
        recovering_ = snapshot_requested_ = false;
        return Result::Applied;
    }
    auto existing = live_.find(p.id);
    const auto tombstone = retired_.find(p.id);
    const auto prior = existing != live_.end() ? std::optional(existing->second.marker.packet.revision) :
                       tombstone != retired_.end() ? std::optional(tombstone->second.revision) : std::nullopt;
    if (prior) {
        if (p.revision == *prior) return Result::Duplicate;
        if (!newer(p.revision, *prior)) return Result::StaleRevision;
    }
    if (p.action == Action::Cancel) {
        if (existing != live_.end()) live_.erase(existing);
        retire(p.id, p.revision, false);
        expire(now);
        return Result::Applied;
    }
    if (existing == live_.end() && p.action == Action::Update &&
        (tombstone == retired_.end() || !tombstone->second.update_may_restart)) return Result::StaleRevision;
    auto replacement = make_entry(p, now);
    if (reached(now, replacement.marker.local_expire)) {
        if (existing != live_.end()) live_.erase(existing);
        retire(p.id, p.revision, true);
        return Result::Expired;
    }
    if (existing == live_.end() && live_.size() == kMaximumActive) {
        request_snapshot(); return Result::Capacity;
    }
    live_.insert_or_assign(p.id, std::move(replacement));
    expire(now);
    return Result::Applied;
}

Result Session::receive(std::span<const std::byte> payload, std::uint32_t now, std::uint32_t receipt_epoch) {
    if (receipt_epoch != epoch_) return Result::WrongEpoch;
    const auto p = decode(payload);
    if (!p || p->action == Action::Hello || p->action == Action::Presented) return Result::Invalid;
    if (!selected_ || !in_zone_ || !epoch_ || !capabilities_.complete_v3()) return Result::NotReady;
    if (p->action == Action::HelloAck) {
        if (!p->start_ms) return Result::Invalid;
        auto hello = std::find_if(hellos_.begin(), hellos_.end(), [&](const Hello& h) {
            return h.used && h.epoch == epoch_ && h.tick == p->start_ms;
        });
        if (hello == hellos_.end()) return Result::Invalid;
        const bool latest = hello->serial == hello_serial_;
        hello->used = false;
        const auto rtt = now - p->start_ms;
        if (rtt <= 10000) {
            const auto sample = p->server_now_ms - (p->start_ms + rtt / 2);
            if (!offset_) offset_ = sample;
            else {
                const auto delta = distance(sample, *offset_);
                const auto magnitude = delta < 0 ? -static_cast<std::int64_t>(delta) : delta;
                offset_ = magnitude > 5000 ? sample : *offset_ + static_cast<std::uint32_t>(delta / 4);
            }
        }
        if (!negotiated_ && !latest) request_snapshot();
        negotiated_ = true;
        next_hello_ = now + (snapshot_requested_ ? 2000u : 30000u);
        return Result::Applied;
    }
    if (!negotiated_) return Result::NotReady;
    expire(now); // Expired entries cannot consume capacity while processing a new packet.
    return apply(*p, now);
}

bool Session::current(const RenderTag& tag, std::uint32_t now, bool before_impact) const {
    const auto entry = live_.find(tag.id);
    return tag.epoch == epoch_ && entry != live_.end() && entry->second.marker.tag == tag &&
           reached(now, entry->second.marker.local_start) && !reached(now, entry->second.marker.local_expire) &&
           (!before_impact || !reached(now, entry->second.marker.local_impact));
}

void Session::pulse(std::uint32_t now, WireSender& sender) {
    expire(now);
    if (!selected_ || !in_zone_ || !epoch_ || !capabilities_.complete_v3()) return;
    if (snapshot_generation_ && now - snapshot_started_ >= 5000) {
        clear_snapshot(); request_snapshot();
    }
    if (ready() && !recovering_ && !snapshot_generation_) {
        if (now - ack_window_ >= 1000) { ack_window_ = now; ack_window_count_ = 0; }
        for (auto it = acknowledgements_.begin(); it != acknowledgements_.end() && ack_window_count_ < 1024;) {
            if (!current(it->tag, now, true)) { it = acknowledgements_.erase(it); continue; }
            Packet proof;
            proof.action = Action::Presented; proof.generation = it->tag.generation;
            proof.id = it->tag.id; proof.revision = it->tag.revision; proof.server_now_ms = it->present_tick;
            const auto bytes = encode(proof);
            if (!bytes || !sender.send(*bytes)) break;
            live_.at(it->tag.id).acknowledged = true;
            ++ack_window_count_; it = acknowledgements_.erase(it);
        }
    }
    if (next_hello_ && !reached(now, *next_hello_)) return;
    const auto tick = now ? now : 1u;
    if (std::any_of(recent_hello_ticks_.begin(), recent_hello_ticks_.end(), [&](const auto& previous) {
        return previous && *previous == tick;
    })) return; // Preserve actual client tick semantics; wait for the clock to advance.
    if (std::any_of(hellos_.begin(), hellos_.end(), [&](const Hello& hello) {
        return hello.used && hello.epoch == epoch_ && hello.tick == tick;
    })) return;
    recent_hello_ticks_[recent_hello_cursor_++ % recent_hello_ticks_.size()] = tick;
    auto& hello = hellos_[hello_cursor_++ % hellos_.size()];
    hello = {true, tick, epoch_, ++hello_serial_};
    Packet request;
    request.action = Action::Hello; request.revision = snapshot_requested_ ? 1u : 0u; request.server_now_ms = tick;
    const auto bytes = encode(request);
    const bool sent = bytes && sender.send(*bytes);
    if (!sent) hello.used = false;
    next_hello_ = now + (!sent || !negotiated_ || snapshot_requested_ ? 2000u : 30000u);
}

RenderFrame Session::prepare_frame(std::uint32_t now) {
    expire(now);
    RenderFrame frame;
    frame.epoch = epoch_;
    if (!ready()) return frame;
    frame.frame = ++frame_serial_;
    auto& issued = issued_[frame_cursor_++ % issued_.size()];
    issued.id = frame.frame; issued.epoch = epoch_; issued.markers.clear();
    for (const auto& [id, entry] : live_) {
        (void)id;
        if (!reached(now, entry.marker.local_start)) continue;
        frame.markers.push_back(entry.marker); issued.markers.push_back(entry.marker.tag);
    }
    return frame;
}

bool Session::rendered(const RenderReceipt& receipt, std::uint32_t now) {
    if (!ready() || recovering_ || snapshot_generation_ || receipt.epoch != epoch_ || !receipt.submitted ||
        !receipt.presented || !receipt.frame || receipt.markers.size() > kMaximumActive) return false;
    auto issued = std::find_if(issued_.begin(), issued_.end(), [&](const IssuedFrame& frame) {
        return frame.id == receipt.frame && frame.epoch == epoch_;
    });
    if (issued == issued_.end()) return false;
    for (const auto& marker : receipt.markers) {
        if (!marker.complete_geometry || !current(marker.tag, now, true) ||
            std::find(issued->markers.begin(), issued->markers.end(), marker.tag) == issued->markers.end()) continue;
        auto& entry = live_.at(marker.tag.id);
        if (!(entry.marker.packet.flags & RequiredDisplay) || !marker.tag.generation || !marker.tag.id || !marker.tag.revision ||
            ((entry.marker.packet.flags & ClampToTerrain) && !marker.terrain_conformed) || entry.acknowledged) continue;
        if (std::none_of(acknowledgements_.begin(), acknowledgements_.end(), [&](const PendingAck& ack) {
            return ack.tag == marker.tag;
        }) && acknowledgements_.size() < kMaximumActive) acknowledgements_.push_back({marker.tag, now});
    }
    issued->id = 0; issued->markers.clear();
    return true;
}

} // namespace bastion::telegraphs
