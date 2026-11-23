#pragma once

#include "telegraphs_protocol.h"

#include <array>
#include <deque>
#include <map>
#include <optional>
#include <vector>

namespace bastion::telegraphs {

struct RendererCapabilities {
    std::uint8_t protocol_version = 0;
    std::uint16_t shapes = 0, flags = 0;
    bool resolves_anchors = false, styles_and_mechanics = false;
    bool per_vertex_terrain = false, presentation_receipts = false;
    bool complete_v3() const noexcept;
};

struct RenderTag {
    std::uint32_t epoch = 0, generation = 0, id = 0, revision = 0;
    // Local provenance prevents an old render completion from certifying an
    // equal wire tuple admitted again by snapshot replacement or ID reuse.
    std::uint64_t incarnation = 0;
    bool operator==(const RenderTag&) const = default;
};
struct RenderMarker {
    Packet packet;
    RenderTag tag;
    std::uint32_t local_start = 0, local_impact = 0, local_expire = 0;
};
struct RenderFrame {
    std::uint32_t epoch = 0;
    std::uint64_t frame = 0;
    std::vector<RenderMarker> markers; // At most 256; owned by this frame.
};
struct MarkerReceipt {
    RenderTag tag;
    bool complete_geometry = false;
    bool terrain_conformed = false;
};
struct RenderReceipt {
    std::uint32_t epoch = 0;
    std::uint64_t frame = 0;
    bool submitted = false, presented = false;
    std::span<const MarkerReceipt> markers;
};

// Internal protocol-side contracts. The SDK adapter translates generic host
// frames/receipts; no graphics or private client types enter this reducer.
class RenderAdapter {
public:
    virtual ~RenderAdapter() = default;
    virtual RendererCapabilities capabilities() const = 0;
    virtual void submit(RenderFrame frame) = 0; // Must later provide an actual presentation receipt.
};
class WireSender {
public:
    virtual ~WireSender() = default;
    virtual bool send(const Bytes& payload) = 0;
};

enum class Result { Applied, Duplicate, StaleGeneration, StaleRevision, Expired, Capacity,
                    SnapshotIncomplete, Invalid, NotReady, WrongEpoch };

class Session {
public:
    // Host-thread reducer. Cross-thread render completions must be copied onto
    // that thread's queue; adapters/senders must not re-enter this Session.
    void context(bool selected_peer, bool in_zone, std::uint32_t epoch);
    void renderer(RendererCapabilities capabilities);
    Result receive(std::span<const std::byte> payload, std::uint32_t local_now, std::uint32_t receipt_epoch);
    void pulse(std::uint32_t local_now, WireSender& sender);
    void request_snapshot();
    RenderFrame prepare_frame(std::uint32_t local_now);
    // True means an issued-frame receipt was consumed. Each marker is checked
    // separately; this does not mean a Presented packet was queued or sent.
    bool rendered(const RenderReceipt& receipt, std::uint32_t local_now);

    bool ready() const { return selected_ && in_zone_ && epoch_ && capabilities_.complete_v3() && negotiated_; }
    bool recovering() const { return recovering_; }
    bool clock_synchronized() const { return offset_.has_value(); }
    bool marker_active(const RenderTag& tag, std::uint32_t now) const { return current(tag, now, false); }
    std::optional<std::uint32_t> server_minus_local() const { return offset_; }
    std::size_t active_count() const { return live_.size(); }
    std::size_t pending_count() const { return staged_.size(); }
    std::size_t queued_ack_count() const { return acknowledgements_.size(); }
    std::optional<std::uint32_t> generation() const { return generation_; }

private:
    struct Entry { RenderMarker marker; bool acknowledged = false; };
    struct Retired { std::uint32_t revision; bool update_may_restart; };
    struct Hello { bool used = false; std::uint32_t tick = 0, epoch = 0; std::uint64_t serial = 0; };
    struct IssuedFrame { std::uint64_t id = 0; std::uint32_t epoch = 0; std::vector<RenderTag> markers; };
    struct PendingAck { RenderTag tag; std::uint32_t present_tick; };

    void reset_protocol();
    void clear_proofs();
    void clear_snapshot();
    void begin_generation(std::uint32_t value);
    void retire(std::uint32_t id, std::uint32_t revision, bool update_may_restart);
    void expire(std::uint32_t local_now);
    Entry make_entry(const Packet& packet, std::uint32_t local_now);
    Result apply(const Packet& packet, std::uint32_t local_now);
    bool current(const RenderTag& tag, std::uint32_t local_now, bool before_impact) const;

    bool selected_ = false, in_zone_ = false, negotiated_ = false;
    bool recovering_ = false, snapshot_requested_ = false;
    std::uint32_t epoch_ = 0;
    RendererCapabilities capabilities_;
    std::optional<std::uint32_t> generation_, snapshot_generation_, offset_, next_hello_;
    std::uint32_t snapshot_started_ = 0;
    std::size_t expected_ = 0;
    std::map<std::uint32_t, Entry> live_, staged_;
    std::map<std::uint32_t, Retired> retired_;
    std::deque<std::uint32_t> retired_order_;
    std::array<Hello, 8> hellos_{};
    std::size_t hello_cursor_ = 0;
    // Keep recent attempted wire ticks across epoch/renderer resets. A new
    // attempt in the same millisecond must not reuse an older ACK's echo token.
    std::array<std::optional<std::uint32_t>, 8> recent_hello_ticks_{};
    std::size_t recent_hello_cursor_ = 0;
    std::uint64_t hello_serial_ = 0, incarnation_ = 0, frame_serial_ = 0;
    std::array<IssuedFrame, 8> issued_{};
    std::size_t frame_cursor_ = 0;
    std::vector<PendingAck> acknowledgements_;
    std::uint32_t ack_window_ = 0, ack_window_count_ = 0;
};

} // namespace bastion::telegraphs
