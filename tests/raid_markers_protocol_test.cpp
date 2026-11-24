#include "raid_markers_protocol.h"
#include "hailstone_byte_io.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace bastion::raid_markers;

namespace {

constexpr std::size_t kPeerOffset = 24;
constexpr std::size_t kPayloadOffset = 88;
constexpr std::size_t kFirstMarkerOffset = 112;
constexpr std::uint32_t kTransfer = 0x12345678;
constexpr std::uint64_t kNonce = 0x1234567890abcdef;
constexpr std::uint32_t kSentinelMagic = 0x534e5431;

void check(bool ok, const char* text) {
    if (!ok) {
        std::cerr << text << '\n';
        std::exit(1);
    }
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i)
        bytes.at(offset + i) = static_cast<std::byte>((value >> (8 * i)) & 0xff);
}

// The zone rewrites the request's destination as its authenticated sender.
Bytes relay(Bytes bytes, std::string_view sender = "RaidCaller") {
    std::fill_n(bytes.begin() + kPeerOffset, 64, std::byte{});
    std::transform(sender.begin(), sender.end(), bytes.begin() + kPeerOffset,
                   [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

Bytes outgoing(const Message& message, std::string_view recipient = {}) {
    const auto result = encode(message, kTransfer, recipient);
    check(result.has_value(), "Valid outgoing message was rejected");
    return *result;
}

void roundtrip(const Message& message, Scope scope) {
    const auto packet = outgoing(message, scope == Scope::Player ? "RaidLeader" : "");
    Received received;
    check(decode(relay(packet), received) == DecodeResult::Valid, "Relayed packet failed to decode");
    check(received.sender == "RaidCaller" && received.scope == scope && received.transfer == kTransfer,
          "Decoded sender, transfer or scope did not match the outer header");
    check(received.message == message, "Message did not survive its roundtrip");
}

void valid_packets() {
    roundtrip({}, Scope::Player);
    roundtrip({Kind::Set, kNonce, 0, 0, {{4, 202, 0, 1.5f, -12.0f, 30.0f}}}, Scope::Player);
    roundtrip({Kind::Clear, kNonce, 0, 8, {}}, Scope::Player);
    roundtrip({Kind::ClearAll, kNonce, 0, 0, {}}, Scope::Player);
    roundtrip({Kind::Snapshot, kNonce, 1, 0, {}}, Scope::Raid);

    Message snapshot{Kind::Snapshot, kNonce, 7, 0, {}};
    for (std::uint8_t id = 1; id <= kMarkerCount; ++id)
        snapshot.markers.push_back({id, 700, id, -kMaximumCoordinate, 0, kMaximumCoordinate});
    roundtrip(snapshot, Scope::Raid);

    const auto packet = outgoing(snapshot);
    hailstone::ByteReader outer(packet);
    check(outer.le<std::uint32_t>() == kSentinelMagic && outer.le<std::uint8_t>() == 1 &&
          outer.le<std::uint8_t>() == static_cast<std::uint8_t>(Scope::Raid) &&
          outer.le<std::uint8_t>() == 2 && outer.le<std::uint8_t>() == 0,
          "Outer envelope does not match the native Sentinel section header");
    check(outer.le<std::uint32_t>() == kTransfer && outer.le<std::uint16_t>() == 0 &&
          outer.le<std::uint16_t>() == 1 && outer.le<std::uint32_t>() == packet.size() - kPayloadOffset &&
          outer.le<std::uint16_t>() == packet.size() - kPayloadOffset && outer.le<std::uint16_t>() == 0,
          "Outer envelope has noncanonical sizes or chunk fields");
    check(packet.size() == 304 && packet.size() <= kPayloadOffset + 400,
          "An eight-marker snapshot must fit in one relay chunk");
    check(outer.fixed_string(64).empty(), "Raid broadcast must have an empty outbound recipient");
    check(outer.le<std::uint32_t>() != kSentinelMagic, "Legacy Sentinel could parse the marker payload");

    const auto request = outgoing({}, "RaidLeader");
    hailstone::ByteReader direct(request);
    direct.seek(kPeerOffset);
    check(direct.fixed_string(64) == "RaidLeader", "Direct request omitted the destination character");
}

void disallowed_messages() {
    check(!encode({}, kTransfer) && !encode({}, kTransfer, "Bad Recipient") &&
          !encode({}, kTransfer, std::string(64, 'A')) && !encode({}, 0, "RaidLeader"),
          "Encoder accepted a missing or malformed recipient or transfer ID");
    check(!encode({Kind::Snapshot, kNonce, 1, 0, {}}, kTransfer, "RaidLeader"),
          "Encoder allowed a snapshot addressed to a single character");
    check(!encode({Kind::RequestSync, kNonce, 0, 0, {}}, kTransfer, "RaidLeader") &&
          !encode({Kind::Clear, kNonce, 0, 9, {}}, kTransfer, "RaidLeader"),
          "Encoder accepted a populated sync or out-of-range clear ID");

    Message snapshot{Kind::Snapshot, kNonce, 1, 0, {{1, 202, 0, 0, 1, 2}, {2, 202, 0, 0, 1, 2}}};
    snapshot.markers[1].id = 1;
    check(!encode(snapshot, kTransfer), "Encoder accepted duplicate marker slots");
    snapshot.markers[0].id = 2;
    check(!encode(snapshot, kTransfer), "Encoder accepted unsorted marker slots");
    snapshot.markers[1].id = 3;
    snapshot.markers[1].zone_id = 0;
    check(!encode(snapshot, kTransfer), "Encoder accepted an unknown zone");
    snapshot.markers[1].zone_id = 202;
    snapshot.markers[1].x = std::numeric_limits<float>::infinity();
    check(!encode(snapshot, kTransfer), "Encoder accepted an infinite coordinate");
    snapshot.markers[1].x = kMaximumCoordinate + 1;
    check(!encode(snapshot, kTransfer), "Encoder accepted an out-of-bounds coordinate");
    snapshot.markers[1].x = 0;
    snapshot.revision = 0;
    check(!encode(snapshot, kTransfer), "Encoder accepted a snapshot without a revision");
}

void malformed_packets() {
    Received received{Scope::Raid, "Previous", 7, {}};
    const auto check_invalid = [&](const Bytes& packet, const char* text) {
        check(decode(packet, received) == DecodeResult::Invalid, text);
        check(received.sender == "Previous" && received.transfer == 7,
              "An invalid packet changed the previously received state");
    };
    const auto snapshot = relay(outgoing({Kind::Snapshot, kNonce, 1, 0,
                                          {{1, 2, 0, 1, 2, 3}, {2, 2, 4, 7, 8, 9}}}));
    const std::array<std::size_t, 15> invalid_offsets{0, 4, 5, 6, 7, 12, 14, 16, 20, 22, 92, 93, 94, 95, 113};
    for (const auto offset : invalid_offsets) {
        auto malformed = snapshot;
        malformed[offset] ^= std::byte{1};
        check_invalid(malformed, "Decoder accepted corrupted header or marker fields");
    }
    auto duplicate = snapshot;
    duplicate[kFirstMarkerOffset + 24] = std::byte{1};
    check_invalid(duplicate, "Decoder accepted a duplicate marker slot");
    auto zero_transfer = snapshot;
    put32(zero_transfer, 8, 0);
    check_invalid(zero_transfer, "Decoder accepted a zero transfer ID");

    auto no_name = relay(snapshot, "");
    check_invalid(no_name, "Decoder accepted an empty authenticated sender");
    check_invalid(relay(snapshot, std::string(64, 'A')), "Decoder accepted an unterminated sender");
    check_invalid(relay(snapshot, "Spoofed Sender"), "Decoder accepted a malformed sender");
    no_name[kPayloadOffset - 1] = std::byte{1};
    check_invalid(no_name, "Decoder accepted dirty sender padding");

    auto nan_coordinate = snapshot;
    put32(nan_coordinate, kFirstMarkerOffset + 12, std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()));
    check_invalid(nan_coordinate, "Decoder accepted a NaN coordinate");
    auto trailing = snapshot;
    trailing.push_back(std::byte{});
    check_invalid(trailing, "Decoder accepted trailing data");
    auto truncated = snapshot;
    truncated.resize(kPayloadOffset + 4);
    check_invalid(truncated, "Recognizable but truncated marker packet fell through");

    auto wrong_scope = relay(outgoing({}, "RaidLeader"));
    wrong_scope[5] = std::byte{2};
    check_invalid(wrong_scope, "Decoder accepted a raid-wide request");
    auto sentinel = snapshot;
    put32(sentinel, kPayloadOffset, kSentinelMagic);
    check(decode(sentinel, received) == DecodeResult::Unrelated,
          "A legacy Sentinel payload was consumed as a marker");
    check(decode({}, received) == DecodeResult::Unrelated, "Truncated unrelated traffic was consumed");
}

} // namespace

int main() {
    valid_packets();
    disallowed_messages();
    malformed_packets();
}
