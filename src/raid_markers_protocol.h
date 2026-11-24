#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bastion::raid_markers {

inline constexpr std::uint16_t kOpcode = 0x7d1e;
inline constexpr std::uint8_t kMarkerCount = 8;
inline constexpr float kMaximumCoordinate = 1000000.0f;

enum class Kind : std::uint8_t { RequestSync = 1, Set, Clear, ClearAll, Snapshot };
enum class Scope : std::uint8_t { Player = 1, Raid = 2 };

struct Marker {
    std::uint8_t id = 0;
    std::uint32_t zone_id = 0;
    std::uint32_t instance_id = 0;
    float x = 0;
    float y = 0;
    float z = 0;

    bool operator==(const Marker&) const = default;
};

struct Message {
    Kind kind = Kind::RequestSync;
    std::uint64_t nonce = 0;
    std::uint64_t revision = 0;
    std::uint8_t marker_id = 0;
    std::vector<Marker> markers;

    bool operator==(const Message&) const = default;
};

struct Received {
    Scope scope = Scope::Player;
    std::string sender;
    std::uint32_t transfer = 0;
    Message message;
};

using Bytes = std::vector<std::byte>;
enum class DecodeResult { Unrelated, Invalid, Valid };

// SNT1 forwards the outer peer field as the authenticated sender on inbound
// packets. The caller must supply a packet received through Bastion's relay.
DecodeResult decode(std::span<const std::byte> bytes, Received& received);

// Snapshots use raid scope. Requests need the raid leader's character name.
// Snapshot markers must be in ascending marker ID order.
std::optional<Bytes> encode(const Message& message, std::uint32_t transfer,
                            std::string_view recipient = {});

} // namespace bastion::raid_markers
