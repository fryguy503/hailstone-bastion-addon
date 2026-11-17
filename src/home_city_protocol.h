#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace bastion::home_city {
inline constexpr std::uint16_t kOpcode = 0x7d23;
enum class Action : std::uint16_t { Probe = 1, List = 2, Commit = 3, Suppress = 4 };
enum class Result : std::uint32_t { Ok = 0, NoGrant, NoChoices, Invalid, Busy, Unavailable, Stale, Unsafe };
struct Entry {
    std::uint32_t choice = 0, zone = 0;
    std::string name;
};
struct Response {
    Action action = Action::List;
    std::uint32_t request = 0;
    Result result = Result::Ok;
    std::uint64_t grant = 0, snapshot = 0;
    std::uint32_t race = 0, class_id = 0, deity = 0, current_home = 0, flags = 0;
    std::string message;
    std::vector<Entry> entries;
};
std::optional<Response> decode(std::span<const std::byte>);
std::vector<std::byte> request(Action, std::uint32_t, std::uint32_t choice = 0, std::uint64_t grant = 0,
                               std::uint64_t snapshot = 0);
} // namespace bastion::home_city
