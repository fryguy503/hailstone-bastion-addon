// SPDX-License-Identifier: MIT
#pragma once

#include "hailstone_byte_io.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bastion::ready_check {

constexpr uint16_t kOpcode = 0x7D26;
constexpr uint32_t kMagic = 0x4B435242;
constexpr uint16_t kVersion = 1;
constexpr std::size_t kRequestSize = 16;
constexpr std::size_t kSnapshotSize = 24;
constexpr std::size_t kMemberSize = 68;
constexpr std::size_t kNameSize = 64;
constexpr std::size_t kMaxMembers = 72;

enum class Action : uint8_t { Hello = 1, Start, Respond, End, Snapshot };
enum class Phase : uint8_t { Idle, Active, Ended, Expired };
enum class Status : uint8_t { NoResponse, Ready, NotReady, Legacy };
enum class Capability : uint8_t { Unknown, Modern, ReadyCheck, Legacy };

constexpr uint8_t kLeader = 0x01;
constexpr uint8_t kCanRespond = 0x02;
constexpr uint8_t kInRaid = 0x04;

struct Member {
    std::string name;
    uint8_t group;
    Status status;
    Capability capability;
};

struct Snapshot {
    Phase phase;
    uint64_t check_id;
    uint32_t remaining_ms;
    uint8_t flags;
    std::vector<Member> members;
};

inline std::vector<std::byte> MakeRequest(Action action, uint64_t check_id = 0, Status choice = Status::NoResponse)
{
    hailstone::ByteWriter packet;
    packet.reserve(kRequestSize);
    packet.le(kMagic);
    packet.le(kVersion);
    packet.le(static_cast<uint8_t>(action));
    packet.le(static_cast<uint8_t>(choice));
    packet.le(check_id);
    return packet.take();
}

inline std::optional<Snapshot> ParseSnapshot(const void* data, std::size_t size)
{
    if (!data || size < kSnapshotSize) return std::nullopt;
    hailstone::ByteReader packet(data, size);
    if (packet.le<uint32_t>() != kMagic || packet.le<uint16_t>() != kVersion ||
        packet.le<uint8_t>() != static_cast<uint8_t>(Action::Snapshot)) {
        return std::nullopt;
    }

    const auto phase = static_cast<Phase>(packet.le<uint8_t>());
    const auto check_id = packet.le<uint64_t>();
    const auto remaining_ms = packet.le<uint32_t>();
    const auto count = packet.le<uint16_t>();
    const auto flags = packet.le<uint8_t>();
    if (packet.le<uint8_t>() != 0 || phase > Phase::Expired ||
        (flags & ~(kLeader | kCanRespond | kInRaid)) != 0 || count > kMaxMembers ||
        packet.remaining() != count * kMemberSize) {
        return std::nullopt;
    }

    const bool in_raid = (flags & kInRaid) != 0;
    if ((phase == Phase::Idle) != (check_id == 0) ||
        (phase != Phase::Active && (remaining_ms != 0 || (flags & kCanRespond) != 0)) ||
        (!in_raid && (flags != 0 || phase != Phase::Idle || count != 0))) {
        return std::nullopt;
    }

    Snapshot result{phase, check_id, remaining_ms, flags, {}};
    result.members.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto name = packet.bytes(kNameSize);
        const auto name_end = std::find(name.begin(), name.end(), std::byte{0});
        const auto group = packet.le<uint8_t>();
        const auto status = static_cast<Status>(packet.le<uint8_t>());
        const auto capability = static_cast<Capability>(packet.le<uint8_t>());
        if (name_end == name.begin() || name_end == name.end() || group > 12 || status > Status::Legacy ||
            capability > Capability::Legacy || packet.le<uint8_t>() != 0 ||
            std::any_of(name.begin(), name_end, [](std::byte c) { return c < std::byte{32} || c == std::byte{127}; })) {
            return std::nullopt;
        }
        if (status == Status::Legacy && capability != Capability::Legacy) {
            return std::nullopt;
        }
        result.members.push_back({
            std::string(reinterpret_cast<const char*>(name.data()), static_cast<std::size_t>(name_end - name.begin())),
            group, status, capability,
        });
    }
    return result;
}

}  // namespace bastion::ready_check
