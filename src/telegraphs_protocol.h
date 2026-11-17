#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace bastion::telegraphs {

inline constexpr std::uint16_t kOpcode = 0x7d1f;
// The two owned peers use this numeric convention, not the ASCII byte order of
// the label TEL1: little-endian bytes are 31 4c 45 54.
inline constexpr std::uint32_t kMagic = 0x54454c31;
inline constexpr std::uint8_t kVersion = 3;
inline constexpr std::size_t kPacketBytes = 96, kMaximumActive = 256;
inline constexpr std::size_t kMaximumPacketBytes = 504;
static_assert(kPacketBytes <= kMaximumPacketBytes);

enum class Action : std::uint8_t {
    Create = 1, Update, Cancel, ClearAll, SnapshotBegin, SnapshotEnd, Hello, HelloAck, Presented
};
enum class Shape : std::uint8_t {
    None, Circle, Donut, Cone, Rectangle, Line, Cross, Stack, Spread, MoveAway, Gaze, Target
};
enum class Anchor : std::uint8_t { World, Owner, Entity, Target };
enum class Disposition : std::uint8_t { Auto, Hostile, Friendly, Neutral, Safe };
enum class Mechanic : std::uint8_t { None, Danger, Stack, Spread, MoveAway, Soak, Split, Gaze, Knockback, Interrupt, Custom };
enum class Style : std::uint8_t { Auto, Solid, Striped, Checker, Gradient, Ring };
enum RenderFlag : std::uint16_t {
    FollowPosition = 1, FollowHeading = 2, Filled = 4, Outline = 8, Pulse = 16,
    Countdown = 32, Directional = 64, ClampToTerrain = 128, RequiredDisplay = 256
};
inline constexpr std::uint16_t kKnownFlags = 0x1ff;
inline constexpr std::uint16_t kAllShapes = 0x0ffe; // Shape 1 through 11.

struct Packet {
    std::uint32_t magic = kMagic;
    std::uint8_t version = kVersion;
    Action action = Action::Create;
    Shape shape = Shape::None;
    Anchor anchor = Anchor::World;
    std::uint16_t flags = 0;
    Disposition disposition = Disposition::Auto;
    Mechanic mechanic = Mechanic::None;
    Style style = Style::Auto;
    std::array<std::uint8_t, 3> reserved{};
    std::uint32_t generation = 0, id = 0, revision = 0, owner_id = 0, anchor_id = 0, spell_id = 0;
    std::uint32_t server_now_ms = 0, start_ms = 0, impact_ms = 0, expire_ms = 0;
    float x = 0, y = 0, z = 0, heading = 0;
    float p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0;
    std::uint32_t color_argb = 0;
};

using Bytes = std::array<std::byte, kPacketBytes>;
bool newer(std::uint32_t candidate, std::uint32_t reference) noexcept;
bool reached(std::uint32_t now, std::uint32_t deadline) noexcept;
std::int32_t distance(std::uint32_t first, std::uint32_t second) noexcept;
bool valid(const Packet& packet) noexcept;
std::optional<Packet> decode(std::span<const std::byte> bytes) noexcept;
std::optional<Bytes> encode(const Packet& packet);

} // namespace bastion::telegraphs
