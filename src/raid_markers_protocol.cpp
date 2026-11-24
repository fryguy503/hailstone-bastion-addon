#include "raid_markers_protocol.h"
#include "hailstone_byte_io.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace bastion::raid_markers {
namespace {

constexpr std::uint32_t kShareMagic = 0x534e5431;
constexpr std::uint8_t kShareVersion = 1;
constexpr std::uint8_t kShareSection = 2;
constexpr std::size_t kShareHeaderBytes = 88;
constexpr std::size_t kShareNameBytes = 64;
constexpr std::size_t kSharePayloadLimit = 400;

// RGM1 distinguishes placement messages from Sentinel section data.
constexpr std::uint32_t kMarkerMagic = 0x314d4752;
constexpr std::uint8_t kMarkerVersion = 1;
constexpr std::size_t kMarkerHeaderBytes = 24;
constexpr std::size_t kMarkerRowBytes = 24;
constexpr std::size_t kMaximumPayload = kMarkerHeaderBytes + kMarkerCount * kMarkerRowBytes;
static_assert(kMaximumPayload <= kSharePayloadLimit);

bool valid_name(std::string_view name) {
    return !name.empty() && name.size() < kShareNameBytes &&
           std::all_of(name.begin(), name.end(), [](char value) {
               return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
                      value == '_' || value == '-' || value == '.';
           });
}

bool valid_id(std::uint8_t id) {
    return id >= 1 && id <= kMarkerCount;
}

bool valid_marker(const Marker& marker) {
    const auto coordinate = [](float value) {
        return std::isfinite(value) && std::abs(value) <= kMaximumCoordinate;
    };
    return valid_id(marker.id) && marker.zone_id &&
           coordinate(marker.x) && coordinate(marker.y) && coordinate(marker.z);
}

bool valid_message(const Message& message) {
    switch (message.kind) {
    case Kind::RequestSync:
        return !message.nonce && !message.revision && !message.marker_id && message.markers.empty();
    case Kind::Set:
        return message.nonce && !message.revision && !message.marker_id &&
               message.markers.size() == 1 && valid_marker(message.markers.front());
    case Kind::Clear:
        return message.nonce && !message.revision && valid_id(message.marker_id) && message.markers.empty();
    case Kind::ClearAll:
        return message.nonce && !message.revision && !message.marker_id && message.markers.empty();
    case Kind::Snapshot: {
        if (!message.nonce || !message.revision || message.marker_id || message.markers.size() > kMarkerCount)
            return false;
        std::uint8_t previous_id = 0;
        for (const Marker& marker : message.markers) {
            if (!valid_marker(marker) || marker.id <= previous_id)
                return false;
            previous_id = marker.id;
        }
        return true;
    }
    }
    return false;
}

Scope scope_for(Kind kind) {
    return kind == Kind::Snapshot ? Scope::Raid : Scope::Player;
}

} // namespace

DecodeResult decode(std::span<const std::byte> bytes, Received& received) {
    if (bytes.size() < kShareHeaderBytes + sizeof(kMarkerMagic))
        return DecodeResult::Unrelated;
    hailstone::ByteReader payload(bytes.subspan(kShareHeaderBytes));
    if (payload.le<std::uint32_t>() != kMarkerMagic)
        return DecodeResult::Unrelated;
    if (payload.size() < kMarkerHeaderBytes || payload.size() > kMaximumPayload)
        return DecodeResult::Invalid;

    hailstone::ByteReader outer(bytes.first(kShareHeaderBytes));
    if (outer.le<std::uint32_t>() != kShareMagic || outer.le<std::uint8_t>() != kShareVersion)
        return DecodeResult::Invalid;
    const auto scope = static_cast<Scope>(outer.le<std::uint8_t>());
    if (outer.le<std::uint8_t>() != kShareSection || outer.le<std::uint8_t>() != 0)
        return DecodeResult::Invalid;
    const auto transfer = outer.le<std::uint32_t>();
    if (!transfer || outer.le<std::uint16_t>() != 0 || outer.le<std::uint16_t>() != 1 ||
        outer.le<std::uint32_t>() != payload.size() || outer.le<std::uint16_t>() != payload.size() ||
        outer.le<std::uint16_t>() != 0)
        return DecodeResult::Invalid;
    const auto name_bytes = outer.bytes(kShareNameBytes);
    const auto terminator = std::find(name_bytes.begin(), name_bytes.end(), std::byte{});
    if (terminator == name_bytes.end() ||
        std::any_of(terminator, name_bytes.end(), [](std::byte b) { return b != std::byte{}; }))
        return DecodeResult::Invalid;
    const std::string_view sender(reinterpret_cast<const char*>(name_bytes.data()),
                                  static_cast<std::size_t>(terminator - name_bytes.begin()));
    if (!valid_name(sender) || payload.le<std::uint8_t>() != kMarkerVersion)
        return DecodeResult::Invalid;

    Message message;
    message.kind = static_cast<Kind>(payload.le<std::uint8_t>());
    message.marker_id = payload.le<std::uint8_t>();
    const auto count = payload.le<std::uint8_t>();
    message.nonce = payload.le<std::uint64_t>();
    message.revision = payload.le<std::uint64_t>();
    if (count > kMarkerCount || payload.remaining() != count * kMarkerRowBytes ||
        scope != scope_for(message.kind))
        return DecodeResult::Invalid;

    message.markers.reserve(count);
    for (std::uint8_t i = 0; i < count; ++i) {
        Marker marker;
        marker.id = payload.le<std::uint8_t>();
        if (payload.le<std::uint8_t>() != 0 || payload.le<std::uint16_t>() != 0)
            return DecodeResult::Invalid;
        marker.zone_id = payload.le<std::uint32_t>();
        marker.instance_id = payload.le<std::uint32_t>();
        marker.x = payload.f32();
        marker.y = payload.f32();
        marker.z = payload.f32();
        message.markers.push_back(marker);
    }
    if (!valid_message(message))
        return DecodeResult::Invalid;

    received = {scope, std::string(sender), transfer, std::move(message)};
    return DecodeResult::Valid;
}

std::optional<Bytes> encode(const Message& message, std::uint32_t transfer, std::string_view recipient) {
    const auto scope = scope_for(message.kind);
    if (!valid_message(message) || !transfer ||
        (scope == Scope::Raid ? !recipient.empty() : !valid_name(recipient)))
        return std::nullopt;

    const auto size = kMarkerHeaderBytes + message.markers.size() * kMarkerRowBytes;
    hailstone::ByteWriter output;
    output.reserve(kShareHeaderBytes + size);
    output.le(kShareMagic);
    output.le(kShareVersion);
    output.le(static_cast<std::uint8_t>(scope));
    output.le(kShareSection);
    output.le<std::uint8_t>(0);
    output.le(transfer);
    output.le<std::uint16_t>(0); // Chunk index.
    output.le<std::uint16_t>(1); // Chunk count.
    output.le(static_cast<std::uint32_t>(size));
    output.le(static_cast<std::uint16_t>(size));
    output.le<std::uint16_t>(0);
    output.fixed_string(recipient, kShareNameBytes);

    output.le(kMarkerMagic);
    output.le(kMarkerVersion);
    output.le(static_cast<std::uint8_t>(message.kind));
    output.le(message.marker_id);
    output.le(static_cast<std::uint8_t>(message.markers.size()));
    output.le(message.nonce);
    output.le(message.revision);
    for (const Marker& marker : message.markers) {
        output.le(marker.id);
        output.zeros(3);
        output.le(marker.zone_id);
        output.le(marker.instance_id);
        output.f32(marker.x);
        output.f32(marker.y);
        output.f32(marker.z);
    }
    return output.take();
}

} // namespace bastion::raid_markers
