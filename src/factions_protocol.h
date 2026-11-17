#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace bastion::factions {
inline constexpr std::uint16_t kOpcode = 0x7d22;
enum class Kind : std::uint16_t { Request = 1, Categories = 2, Entries = 3, Unchanged = 4, Error = 5 };
struct Category {
    std::uint32_t id = 0, parent = 0;
    std::int32_t order = 0;
    std::string name, icon;
    std::array<std::byte, 132> wire{};
};
struct Entry {
    std::int32_t id = 0, earned = 0, minimum = 0, maximum = 0, effective = 0, next = 0;
    std::uint8_t reaction = 0;
    std::vector<std::uint32_t> categories;
    std::string name;
    std::array<std::byte, 252> wire{};
};
struct Packet {
    Kind kind = Kind::Error;
    std::uint32_t request = 0, session = 0, character = 0, result = 0, categories = 0, entries = 0, offset = 0;
    std::uint64_t hash = 0;
    std::vector<Category> category_rows;
    std::vector<Entry> entry_rows;
};
std::optional<Packet> decode(std::span<const std::byte>);
std::vector<std::byte> request(std::uint32_t, std::uint32_t, std::uint64_t known = 0);
bool validate(const std::vector<Category> &, const std::vector<Entry> &, std::uint64_t hash);
bool belongs(const Entry &, std::uint32_t, const std::vector<Category> &);
int gauge(const Entry &);
const char *reaction_name(std::uint8_t);
} // namespace bastion::factions
