#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace bastion::saved_expeditions {
inline constexpr std::uint16_t kOpcode = 0x7d21;
enum class Action : std::uint16_t { List = 1, Resume = 2, ListResult = 3, ResumeResult = 4 };
enum class Result : std::uint32_t {
    Ok = 0,
    Disabled,
    InvalidRequest,
    NotFound,
    NoPermission,
    Expired,
    InExpedition,
    Locked,
    Full,
    Lockout,
    Season,
    PendingInvite,
    Busy,
    Unavailable,
    AlreadyMember,
    MustLeaveInstance
};
struct Entry {
    std::uint32_t dz_id = 0, zone_id = 0, zone_version = 0, instance_id = 0, members = 0, max_members = 0,
                  leader_id = 0;
    Result eligibility = Result::Ok;
    std::uint64_t expires = 0;
    std::string uuid, name, leader;
};
struct Response {
    Action action = Action::ListResult;
    std::uint32_t request = 0;
    Result result = Result::Ok;
    std::uint64_t server_time = 0;
    std::string message;
    std::vector<Entry> entries;
};
bool valid_uuid(std::string_view);
std::optional<Response> decode(std::span<const std::byte>);
std::vector<std::byte> request(Action, std::uint32_t id, const Entry *entry = nullptr);
const char *result_text(Result);
} // namespace bastion::saved_expeditions
