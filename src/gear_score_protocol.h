#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace bastion::gear_score {
inline constexpr std::uint16_t kOpcode = 0x7d24;
inline constexpr std::uint32_t kMagic = 0x31564c49;
inline constexpr std::uint32_t kMaximumMilli = 1000000000;
inline constexpr std::size_t kPacketBytes = 68, kMaximumTemplates = 16384, kMaximumConfigurations = 4096;
enum class Action : std::uint8_t { Hello = 1, HelloAck, Reset, Item, Summary };
enum Flag : std::uint16_t {
    BaseValid = 1, ConfigurationValid = 2, NotGear = 4, EquippedValid = 8,
    OwnedValid = 16, OwnedStale = 32, ReviewRequired = 64,
};
struct Key {
    std::uint32_t item = 0;
    std::array<std::uint32_t, 6> augments{};
    std::uint32_t reference = 0;
    auto operator<=>(const Key&) const = default;
};
struct Packet {
    Action action = Action::Hello;
    std::uint16_t flags = 0;
    std::uint32_t session = 0, generation = 0, revision = 0;
    Key key;
    std::uint32_t base = 0, configuration = 0, equipped = 0, owned = 0;
};
bool valid(const Packet& packet);
std::optional<Packet> decode(std::span<const std::byte> bytes);
// This informational protocol sends only Hello from the client.
std::vector<std::byte> hello(std::uint32_t session);
} // namespace bastion::gear_score
