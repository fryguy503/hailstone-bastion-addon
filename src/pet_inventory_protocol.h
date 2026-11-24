#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace bastion::pet_inventory {
inline constexpr std::uint16_t kOpcode = 0x7bfa, kItemPacket = 0x368e, kDeleteItem = 0x18ad;
inline constexpr std::array<std::uint32_t, 18> kSlots = {1,  2,  3,  5,  6,  7,  8,  9,  11,
                                                         12, 13, 14, 15, 16, 17, 18, 19, 20};
enum class Action : std::uint32_t { Equip = 1, Refresh = 2, Remove = 3, Status = 4 };
enum StatusFlag : std::uint32_t { ManagedPet = 1, Locked = 2, Charmed = 4, Casting = 8 };
struct Status {
    std::uint32_t flags = 0, remaining_ms = 0, managed = 0, occupied = 0;
    bool ownership_known = false;
    std::string name;
    bool operator==(const Status &) const = default;
};
bool supported(std::uint32_t);
const char *slot_name(std::uint32_t);
std::optional<Status> decode(std::span<const std::byte>);
std::optional<std::uint32_t> deleted_slot(std::span<const std::byte>);
std::vector<std::byte> request(Action, std::uint32_t slot = 0, std::uint32_t count = 0);
} // namespace bastion::pet_inventory
