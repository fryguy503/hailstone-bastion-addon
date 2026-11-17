#include "telegraphs_protocol.h"
#include "telegraphs_state.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace tel = bastion::telegraphs;
namespace {
std::size_t checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void put(tel::Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i != 4; ++i) bytes[at + i] = std::byte(static_cast<std::uint8_t>(value >> (i * 8)));
}
tel::Bytes control(tel::Action action, std::uint32_t generation = 7, std::uint32_t revision = 0) {
    tel::Bytes bytes{};
    bytes[0] = std::byte{0x31}; bytes[1] = std::byte{0x4c}; bytes[2] = std::byte{0x45}; bytes[3] = std::byte{0x54};
    bytes[4] = std::byte{3}; bytes[5] = std::byte(static_cast<std::uint8_t>(action));
    put(bytes, 16, generation); put(bytes, 24, revision); put(bytes, 40, 1010);
    return bytes;
}
tel::Bytes circle(std::uint32_t id = 42, std::uint32_t revision = 3, std::uint32_t generation = 7) {
    // A literal server-side circle, including the numeric magic's byte order,
    // 70000 spell ID, negative Y, required terrain flags and all 96 offsets.
    constexpr std::array<std::uint8_t, 96> literal = {
        0x31,0x4c,0x45,0x54, 3,1,1,0, 0x8c,1,1,1, 1,0,0,0,
        7,0,0,0, 42,0,0,0, 3,0,0,0, 5,0,0,0,
        0,0,0,0, 0x70,0x11,1,0, 0xf2,3,0,0, 0xf2,3,0,0,
        0xda,7,0,0, 0xce,9,0,0, 0,0,0xc0,0x3f, 0,0,0,0xc0,
        0,0,0x40,0x40, 0,0,0,0x43, 0,0,0x20,0x41, 0,0,0,0,
        0,0,0,0, 0,0,0x80,0x41, 0,0,0,0, 0x38,0x43,0xff,0xa8
    };
    tel::Bytes bytes;
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = std::byte(literal[i]);
    put(bytes, 16, generation); put(bytes, 20, id); put(bytes, 24, revision);
    return bytes;
}
tel::RendererCapabilities full_renderer() { return {3, tel::kAllShapes, tel::kKnownFlags, true, true, true, true}; }
struct Sink final : tel::WireSender {
    std::vector<tel::Bytes> packets;
    bool succeeds = true;
    bool send(const tel::Bytes& payload) override {
        if (!succeeds) return false;
        packets.push_back(payload);
        return true;
    }
};
struct Rig {
    tel::Session session;
    Sink sink;
    Rig() { session.context(true, true, 1); session.renderer(full_renderer()); }
    void connect(std::uint32_t tick = 100, std::uint32_t now = 120, std::uint32_t server = 1000) {
        session.pulse(tick, sink);
        check(!sink.packets.empty(), "Capable selected zone did not send Hello");
        auto ack = control(tel::Action::HelloAck);
        put(ack, 44, tick ? tick : 1); put(ack, 40, server);
        check(session.receive(ack, now, 1) == tel::Result::Applied, "Matching HelloAck was rejected");
        check(session.ready(), "Matching renderer handshake did not become ready");
    }
    void add(std::uint32_t id = 42, std::uint32_t revision = 3, std::uint32_t generation = 7) {
        check(session.receive(circle(id, revision, generation), 120, 1) == tel::Result::Applied, "Literal marker was not applied");
    }
};
tel::RenderReceipt receipt(const tel::RenderFrame& frame, std::span<const tel::MarkerReceipt> markers) {
    return {frame.epoch, frame.frame, true, true, markers};
}

void literal_protocol() {
    const auto bytes = circle();
    const auto packet = tel::decode(bytes);
    check(packet.has_value(), "Literal 96-byte v3 packet was rejected");
    check(packet->magic == 0x54454c31 && packet->shape == tel::Shape::Circle && packet->flags == 0x18c,
          "Envelope or flags used the wrong byte order");
    check(packet->generation == 7 && packet->id == 42 && packet->revision == 3 && packet->spell_id == 70000,
          "Full-width identity offsets were wrong");
    check(packet->server_now_ms == 1010 && packet->start_ms == 1010 && packet->impact_ms == 2010 && packet->expire_ms == 2510,
          "Clock fields were decoded at wrong offsets");
    check(packet->x == 1.5f && packet->y == -2 && packet->z == 3 && packet->heading == 128 &&
          packet->p1 == 10 && packet->p4 == 16 && packet->color_argb == 0xa8ff4338,
          "Geometry or color fields were decoded at wrong offsets");
    check(tel::encode(*packet) == bytes, "Encoder did not reproduce the independent literal packet");
    for (const auto length : {std::size_t{0}, std::size_t{95}, std::size_t{97}, std::size_t{504}}) {
        std::vector<std::byte> malformed(length);
        check(!tel::decode(malformed), "Non-96-byte frame was accepted");
    }
    for (std::size_t at : {0u, 4u, 5u, 6u, 7u, 9u, 10u, 11u, 12u, 13u, 14u, 15u}) {
        auto malformed = bytes;
        malformed[at] = std::byte{0xff};
        check(!tel::decode(malformed), "Invalid enum, version, reserved field or flag was accepted");
    }
    for (std::size_t at = 56; at <= 88; at += 4) {
        auto malformed = bytes;
        put(malformed, at, 0x7fc00000); // quiet NaN, independently authored bits
        check(!tel::decode(malformed), "Non-finite geometry was accepted");
        put(malformed, at, 0x7f800000);
        check(!tel::decode(malformed), "Infinite geometry was accepted");
    }
    auto malformed = bytes;
    put(malformed, 84, 0x40800000); // 4 segments
    check(!tel::decode(malformed), "Segment count below 8 was accepted");
    put(malformed, 84, 0x41080000); // 8.5 segments
    check(!tel::decode(malformed), "Fractional segment count was accepted");
    malformed = bytes; put(malformed, 48, 1010);
    check(!tel::decode(malformed), "Zero warning duration was accepted");
    malformed = bytes; put(malformed, 52, 2009);
    check(!tel::decode(malformed), "Expiry before impact was accepted");
    malformed = bytes; put(malformed, 44, 601011);
    check(!tel::decode(malformed), "Start more than ten minutes ahead was accepted");
    for (auto shape : {tel::Shape::Donut, tel::Shape::Cone, tel::Shape::Rectangle, tel::Shape::Line, tel::Shape::Cross}) {
        auto geometry = *packet; geometry.shape = shape;
        if (shape != tel::Shape::Donut) check(!tel::valid(geometry), "Shape accepted missing positive dimensions");
        geometry.p2 = shape == tel::Shape::Cone ? 90.0f : 2.0f;
        check(tel::valid(geometry), "Valid shape dimensions were rejected");
    }
    check(tel::newer(1, 0xfffffffe) && !tel::newer(0xfffffffe, 1) && !tel::newer(0x80000000, 0),
          "Serial wrap or half-range ambiguity was mishandled");
    check(tel::reached(5, 0xfffffffe) && !tel::reached(0xfffffffe, 5), "Clock wrap was mishandled");
}

void handshake_and_clock() {
    Rig rig;
    rig.session.renderer({});
    rig.session.pulse(100, rig.sink);
    check(rig.sink.packets.empty() && !rig.session.ready(), "No renderer advertised readiness");
    auto incomplete = full_renderer(); incomplete.per_vertex_terrain = false;
    rig.session.renderer(incomplete); rig.session.pulse(100, rig.sink);
    check(rig.sink.packets.empty(), "Flat renderer claimed v3 terrain support");
    incomplete = full_renderer(); incomplete.presentation_receipts = false;
    rig.session.renderer(incomplete); rig.session.pulse(100, rig.sink);
    check(rig.sink.packets.empty(), "Renderer without present receipts advertised readiness");
    rig.session.renderer(full_renderer());
    check(rig.session.receive(circle(), 100, 1) == tel::Result::NotReady, "Pre-handshake marker was admitted");
    auto ack = control(tel::Action::HelloAck); put(ack, 44, 99);
    check(rig.session.receive(ack, 120, 1) == tel::Result::Invalid, "Unsolicited HelloAck was admitted");
    rig.connect();
    const auto hello = tel::decode(rig.sink.packets.front());
    check(hello && hello->action == tel::Action::Hello && hello->server_now_ms == 100 && !hello->generation && !hello->revision,
          "Hello did not preserve the owned envelope");
    check(rig.session.clock_synchronized() && rig.session.server_minus_local() == 890u,
          "RTT midpoint did not establish the server clock offset");
    put(ack, 44, 100);
    check(rig.session.receive(ack, 125, 1) == tel::Result::Invalid, "Replay HelloAck was accepted");
    rig.add();
    const auto frame = rig.session.prepare_frame(120);
    check(frame.markers.size() == 1 && frame.markers[0].local_start == 120 &&
          frame.markers[0].local_impact == 1120 && frame.markers[0].local_expire == 1620,
          "Synchronized server deadlines did not map into the local clock");
    rig.session.pulse(30120, rig.sink);
    check(rig.sink.packets.size() == 2, "Periodic 30-second clock resync did not send Hello");
    ack = control(tel::Action::HelloAck); put(ack, 44, 30120); put(ack, 40, 31080);
    check(rig.session.receive(ack, 30160, 1) == tel::Result::Applied, "Clock resync Ack was rejected");
    check(rig.session.server_minus_local() == 902u, "Small clock sample did not use one-quarter smoothing");
    rig.session.context(true, true, 2);
    check(!rig.session.ready() && !rig.session.clock_synchronized() && rig.session.active_count() == 0,
          "New zone retained readiness, clock or markers");
    check(rig.session.receive(ack, 30161, 1) == tel::Result::WrongEpoch, "Old ingress epoch was accepted");
    check(rig.session.receive(ack, 30161, 2) == tel::Result::Invalid, "Old token authenticated a new zone");
    rig.session.context(false, true, 2); rig.session.pulse(31000, rig.sink);
    check(rig.sink.packets.size() == 2, "Unselected peer sent Hello");

    Rig delayed;
    delayed.session.pulse(100, delayed.sink);
    delayed.session.pulse(2100, delayed.sink);
    check(delayed.sink.packets.size() == 2, "Unacknowledged Hello did not retry after two seconds");
    ack = control(tel::Action::HelloAck); put(ack, 44, 100); put(ack, 40, 3020);
    check(delayed.session.receive(ack, 2120, 1) == tel::Result::Applied && delayed.session.recovering(),
          "Delayed initial Ack did not require a replacement snapshot");
    delayed.session.pulse(4120, delayed.sink);
    check(tel::decode(delayed.sink.packets.back())->revision == 1, "Snapshot recovery Hello omitted revision1");
    auto inbound = control(tel::Action::Hello, 0);
    check(delayed.session.receive(inbound, 4130, 1) == tel::Result::Invalid, "Server Hello entered the client reducer");
    inbound = control(tel::Action::Presented); put(inbound, 20, 42); put(inbound, 24, 3);
    check(delayed.session.receive(inbound, 4130, 1) == tel::Result::Invalid, "Inbound Presented advanced client state");
}

void revisions_cancel_and_expiry() {
    Rig rig; rig.connect(); rig.add();
    check(rig.session.receive(circle(), 120, 1) == tel::Result::Duplicate, "Equal revision was not idempotent");
    auto old = circle(42, 2);
    check(rig.session.receive(old, 120, 1) == tel::Result::StaleRevision, "Older revision replaced live state");
    auto update = circle(42, 4); update[5] = std::byte{2};
    check(rig.session.receive(update, 120, 1) == tel::Result::Applied, "Newer Update was rejected");
    auto cancel = control(tel::Action::Cancel, 7, 5); put(cancel, 20, 42);
    check(rig.session.receive(cancel, 130, 1) == tel::Result::Applied && !rig.session.active_count(), "Cancel did not retire its marker");
    update = circle(42, 6); update[5] = std::byte{2};
    check(rig.session.receive(update, 130, 1) == tel::Result::StaleRevision, "Update resurrected a cancelled marker");
    check(rig.session.receive(circle(42, 6), 130, 1) == tel::Result::Applied, "Newer Create could not deliberately reuse a retired ID");
    rig.session.pulse(1620, rig.sink);
    check(!rig.session.active_count(), "Local expiry retained a marker");
    update = circle(42, 7); update[5] = std::byte{2};
    put(update, 40, 2520); put(update, 44, 2520); put(update, 48, 3520); put(update, 52, 4020);
    check(rig.session.receive(update, 1630, 1) == tel::Result::Applied, "Newer in-flight Update could not restart a locally expired marker");
    check(rig.session.receive(circle(99, 1, 6), 1630, 1) == tel::Result::StaleGeneration, "Old generation entered live state");
    auto clear = control(tel::Action::ClearAll, 8);
    check(rig.session.receive(clear, 1640, 1) == tel::Result::Applied && !rig.session.active_count(), "New generation ClearAll retained old state");

    Rig wrap; wrap.connect(); wrap.add(1, 0xfffffffe, 0xfffffffe);
    check(wrap.session.receive(circle(1, 1, 0xfffffffe), 120, 1) == tel::Result::Applied, "Revision wrap was rejected");
    check(wrap.session.receive(circle(2, 1, 1), 120, 1) == tel::Result::Applied && wrap.session.active_count() == 1,
          "Generation wrap did not retire the old set");
}

void snapshots_and_capacity() {
    Rig rig; rig.connect(); rig.add(1);
    const auto old_frame = rig.session.prepare_frame(120);
    const std::array old_receipt{tel::MarkerReceipt{old_frame.markers[0].tag, true, true}};
    check(rig.session.receive(control(tel::Action::SnapshotBegin, 8, 2), 120, 1) == tel::Result::Applied, "SnapshotBegin failed");
    check(!rig.session.rendered(receipt(old_frame, old_receipt), 121), "Snapshot staging admitted an old-live proof");
    check(rig.session.receive(circle(2, 1, 8), 120, 1) == tel::Result::Applied, "First snapshot row failed");
    check(rig.session.active_count() == 1 && rig.session.pending_count() == 1, "Snapshot row leaked into the live set");
    check(rig.session.receive(circle(2, 1, 8), 120, 1) == tel::Result::Duplicate && rig.session.pending_count() == 1,
          "Duplicate snapshot row changed its count");
    check(rig.session.receive(circle(3, 1, 8), 120, 1) == tel::Result::Applied, "Second snapshot row failed");
    check(rig.session.receive(control(tel::Action::SnapshotEnd, 8, 2), 120, 1) == tel::Result::Applied &&
          rig.session.active_count() == 2 && !rig.session.pending_count(), "Complete snapshot did not commit atomically");
    check(!rig.session.rendered(receipt(old_frame, old_receipt), 122), "Replacement snapshot admitted a retired proof");
    rig.session.receive(control(tel::Action::SnapshotBegin, 8, 2), 130, 1);
    rig.session.receive(circle(4, 1, 8), 130, 1);
    check(rig.session.receive(control(tel::Action::SnapshotEnd, 8, 2), 130, 1) == tel::Result::SnapshotIncomplete &&
          rig.session.recovering() && rig.session.active_count() == 2 && !rig.session.pending_count(),
          "Partial snapshot corrupted the live set or omitted recovery");
    rig.session.receive(control(tel::Action::SnapshotBegin, 8, 1), 140, 1);
    rig.session.receive(circle(4, 1, 8), 140, 1);
    check(rig.session.receive(control(tel::Action::SnapshotEnd, 8, 0), 140, 1) == tel::Result::Applied &&
          rig.session.active_count() == 1 && !rig.session.recovering(), "Compatible zero-count SnapshotEnd failed");
    rig.session.receive(control(tel::Action::SnapshotBegin, 8, 1), 150, 1);
    check(rig.session.receive(control(tel::Action::SnapshotEnd, 7, 1), 150, 1) == tel::Result::StaleGeneration,
          "Mismatched snapshot end was accepted");
    rig.session.pulse(5150, rig.sink);
    check(rig.session.recovering() && !rig.session.pending_count() && tel::decode(rig.sink.packets.back())->revision == 1,
          "Snapshot watchdog failed to request recovery");

    Rig bounded; bounded.connect();
    bool filled = true;
    for (std::uint32_t id = 1; id <= 256; ++id)
        filled = filled && bounded.session.receive(circle(id), 120, 1) == tel::Result::Applied;
    check(filled && bounded.session.active_count() == 256, "Valid active capacity was rejected");
    check(bounded.session.receive(circle(257), 120, 1) == tel::Result::Capacity && bounded.session.recovering() &&
          bounded.session.active_count() == 256, "Active overflow was unbounded or silently evicted a marker");
    bounded.session.receive(control(tel::Action::SnapshotBegin, 8, 1), 120, 1);
    bounded.session.receive(circle(1, 1, 8), 120, 1);
    check(bounded.session.receive(circle(2, 1, 8), 120, 1) == tel::Result::Capacity && !bounded.session.pending_count(),
          "Declared snapshot count overflow escaped its bound");

    Rig expired; expired.connect();
    for (std::uint32_t id = 1; id <= 256; ++id) expired.session.receive(circle(id), 120, 1);
    auto fresh = circle(257);
    put(fresh, 40, 2510); put(fresh, 44, 2510); put(fresh, 48, 3510); put(fresh, 52, 4010);
    check(expired.session.receive(fresh, 1620, 1) == tel::Result::Applied && expired.session.active_count() == 1 &&
          !expired.session.recovering(), "Expired full set falsely exhausted capacity before the next pulse");
}

void presentation_provenance() {
    Rig rig; rig.connect(); rig.add();
    rig.session.pulse(125, rig.sink);
    check(rig.sink.packets.size() == 1, "Packet receipt alone emitted Presented");
    auto frame = rig.session.prepare_frame(120);
    std::array markers{tel::MarkerReceipt{frame.markers[0].tag, true, true}};
    auto proof = receipt(frame, markers);
    proof.presented = false;
    check(!rig.session.rendered(proof, 125) && !rig.session.queued_ack_count(), "Submission alone admitted presentation proof");
    proof.presented = true; markers[0].complete_geometry = false;
    check(rig.session.rendered(proof, 125) && !rig.session.queued_ack_count(), "Partial geometry admitted presentation proof");
    frame = rig.session.prepare_frame(125); markers[0] = {frame.markers[0].tag, true, false};
    check(rig.session.rendered(receipt(frame, markers), 126) && !rig.session.queued_ack_count(), "Flat terrain admitted required v3 proof");
    frame = rig.session.prepare_frame(126); markers[0] = {frame.markers[0].tag, true, true};
    check(rig.session.rendered(receipt(frame, markers), 127) && rig.session.queued_ack_count() == 1, "Complete presented marker did not queue proof");
    rig.sink.succeeds = false; rig.session.pulse(128, rig.sink);
    check(rig.session.queued_ack_count() == 1, "Failed proof send was treated as delivered");
    rig.sink.succeeds = true; rig.session.pulse(129, rig.sink);
    check(rig.sink.packets.size() == 2 && !rig.session.queued_ack_count(), "Queued proof was not sent exactly once");
    auto ack = tel::decode(rig.sink.packets.back());
    check(ack && ack->action == tel::Action::Presented && ack->generation == 7 && ack->id == 42 &&
          ack->revision == 3 && ack->server_now_ms == 127 && !ack->start_ms && ack->shape == tel::Shape::None,
          "Presented envelope did not preserve exact current identity and present tick");
    frame = rig.session.prepare_frame(130); markers[0] = {frame.markers[0].tag, true, true};
    rig.session.rendered(receipt(frame, markers), 131); rig.session.pulse(132, rig.sink);
    check(rig.sink.packets.size() == 2, "Duplicate render presentation emitted another proof");

    Rig retired; retired.connect(); retired.add();
    frame = retired.session.prepare_frame(120); markers[0] = {frame.markers[0].tag, true, true};
    auto cancel = control(tel::Action::Cancel, 7, 4); put(cancel, 20, 42);
    retired.session.receive(cancel, 121, 1);
    check(retired.session.rendered(receipt(frame, markers), 122) && !retired.session.queued_ack_count(), "Cancelled render tag was certified");
    retired.session.receive(circle(42, 5), 123, 1);
    frame = retired.session.prepare_frame(123); markers[0] = {frame.markers[0].tag, true, true};
    retired.session.rendered(receipt(frame, markers), 124);
    cancel = control(tel::Action::Cancel, 7, 6); put(cancel, 20, 42);
    retired.session.receive(cancel, 125, 1); retired.session.pulse(126, retired.sink);
    check(retired.sink.packets.size() == 1 && !retired.session.queued_ack_count(), "Cancel between receipt and wire send leaked proof");

    Rig snapshot; snapshot.connect(); snapshot.add();
    frame = snapshot.session.prepare_frame(120); markers[0] = {frame.markers[0].tag, true, true};
    snapshot.session.receive(control(tel::Action::SnapshotBegin, 7, 1), 120, 1);
    snapshot.session.receive(circle(), 120, 1);
    snapshot.session.receive(control(tel::Action::SnapshotEnd, 7, 1), 120, 1);
    check(!snapshot.session.rendered(receipt(frame, markers), 121) && !snapshot.session.queued_ack_count(),
          "Same wire tuple from replacement snapshot reused old frame provenance");
    const auto fresh = snapshot.session.prepare_frame(121);
    check(fresh.markers[0].tag.incarnation != markers[0].tag.incarnation, "Snapshot replacement reused local marker incarnation");
    frame = fresh;
    for (unsigned i = 0; i < 8; ++i) snapshot.session.prepare_frame(122 + i);
    markers[0] = {frame.markers[0].tag, true, true};
    check(!snapshot.session.rendered(receipt(frame, markers), 131), "Evicted frame receipt escaped the bounded provenance ledger");
    frame = snapshot.session.prepare_frame(132); markers[0] = {frame.markers[0].tag, true, true};
    check(snapshot.session.rendered(receipt(frame, markers), 1120) && !snapshot.session.queued_ack_count(),
          "Proof at or after impact was admitted");
    snapshot.session.renderer({});
    check(!snapshot.session.ready() && snapshot.session.prepare_frame(140).markers.empty(), "Lost renderer retained readiness or render work");
    snapshot.session.renderer(full_renderer());
    snapshot.session.pulse(200, snapshot.sink);
    check(tel::decode(snapshot.sink.packets.back())->revision == 1,
          "Restored renderer failed to request markers from an already-ready server");
    auto restored_ack = control(tel::Action::HelloAck); put(restored_ack, 44, 200); put(restored_ack, 40, 1100);
    check(snapshot.session.receive(restored_ack, 220, 1) == tel::Result::Applied && snapshot.session.recovering(),
          "Renderer recovery Ack prematurely restored presentation qualification");
    snapshot.session.receive(control(tel::Action::SnapshotBegin, 7, 1), 220, 1);
    snapshot.session.receive(circle(), 220, 1);
    check(snapshot.session.receive(control(tel::Action::SnapshotEnd, 7, 1), 220, 1) == tel::Result::Applied &&
          snapshot.session.active_count() == 1 && !snapshot.session.recovering(),
          "Fresh replacement snapshot did not restore unchanged markers after renderer recovery");
}

void additional_handshake_and_receipt_boundaries() {
    Rig incapable;
    std::array<tel::RendererCapabilities, 5> missing{};
    missing.fill(full_renderer());
    missing[0].protocol_version = 2;
    missing[1].shapes = 1u << 1; // Circle-only renderer cannot negotiate all TEL1 v3.
    missing[2].flags &= ~tel::RequiredDisplay;
    missing[3].resolves_anchors = false;
    missing[4].styles_and_mechanics = false;
    for (const auto& capabilities : missing) {
        incapable.session.renderer(capabilities); incapable.session.pulse(100, incapable.sink);
        check(incapable.sink.packets.empty() && !incapable.session.ready(), "Partial renderer advertised complete v3 readiness");
    }

    Rig failed;
    failed.sink.succeeds = false; failed.session.pulse(100, failed.sink);
    auto ack = control(tel::Action::HelloAck); put(ack, 44, 100);
    check(failed.session.receive(ack, 120, 1) == tel::Result::Invalid, "Failed Hello send left an admissible token");
    failed.sink.succeeds = true; failed.session.pulse(2100, failed.sink);
    check(failed.sink.packets.size() == 1, "Failed Hello send suppressed its retry");

    Rig same_tick;
    same_tick.session.pulse(100, same_tick.sink);
    same_tick.session.context(true, true, 2);
    same_tick.session.pulse(100, same_tick.sink);
    check(same_tick.sink.packets.size() == 1, "New epoch reused a previous attempt's same-millisecond Hello tick");
    check(same_tick.session.receive(ack, 120, 2) == tel::Result::Invalid && !same_tick.session.ready(),
          "Previous-epoch ACK authenticated a same-tick replacement attempt");
    same_tick.session.pulse(101, same_tick.sink);
    check(same_tick.sink.packets.size() == 2, "Same-tick guard prevented the next real-tick attempt");
    check(same_tick.session.receive(ack, 121, 2) == tel::Result::Invalid, "Old ACK authenticated the next unique tick");
    put(ack, 44, 101);
    check(same_tick.session.receive(ack, 121, 2) == tel::Result::Applied && same_tick.session.ready(),
          "Unique current-epoch ACK was rejected");
    put(ack, 44, 100);

    Rig slow;
    slow.session.pulse(100, slow.sink);
    check(slow.session.receive(ack, 10101, 1) == tel::Result::Applied && slow.session.ready() && !slow.session.clock_synchronized(),
          "Over-10-second RTT sample altered the clock or changed the owned handshake policy");

    Rig bounded;
    for (std::uint32_t i = 0; i < 9; ++i) bounded.session.pulse(100 + i * 2000, bounded.sink);
    check(bounded.session.receive(ack, 16101, 1) == tel::Result::Invalid, "Evicted Hello token escaped the eight-entry bound");

    Rig wrapping;
    wrapping.connect(0xfffffff0, 16, 1000);
    check(wrapping.session.server_minus_local() == 1000u, "RTT midpoint clock synchronization failed across tick wrap");

    Rig rig; rig.connect(); rig.add();
    auto frame = rig.session.prepare_frame(120);
    std::array markers{tel::MarkerReceipt{frame.markers[0].tag, true, true}};
    auto proof = receipt(frame, markers);
    proof.epoch = 2;
    check(!rig.session.rendered(proof, 121), "Wrong-epoch receipt was admitted");
    proof = receipt(frame, markers); proof.frame += 1000;
    check(!rig.session.rendered(proof, 121), "Unissued frame was admitted");
    proof = receipt(frame, markers); markers[0].tag.revision += 1;
    check(rig.session.rendered(proof, 121) && !rig.session.queued_ack_count(), "Receipt forged a newer marker revision");
    frame = rig.session.prepare_frame(122); markers[0] = {frame.markers[0].tag, true, true};
    check(rig.session.rendered(receipt(frame, markers), 123) && rig.session.queued_ack_count() == 1, "Valid current proof was not queued");
    rig.session.context(true, true, 2); rig.session.pulse(124, rig.sink);
    check(rig.session.queued_ack_count() == 0 && tel::decode(rig.sink.packets.back())->action == tel::Action::Hello,
          "Zone transition leaked a queued previous-zone Presented packet");

    Rig zero; zero.connect(); zero.add(42, 0, 0);
    frame = zero.session.prepare_frame(120); markers[0] = {frame.markers[0].tag, true, true};
    check(zero.session.rendered(receipt(frame, markers), 121) && !zero.session.queued_ack_count(),
          "Zero generation/revision produced a server-inadmissible proof");
}
}

int main() {
    try {
        literal_protocol(); handshake_and_clock(); revisions_cancel_and_expiry();
        snapshots_and_capacity(); presentation_provenance();
        additional_handshake_and_receipt_boundaries();
        std::cout << "PASS: " << checks << " TEL1 v3 protocol/state checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
