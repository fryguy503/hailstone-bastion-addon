#pragma once

#include "hailstone_plugin.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

namespace bastion::raid {
inline constexpr std::size_t MaxMembers = 72;

struct Snapshot {
    HS_BulkRaidHeader header{};
    std::vector<HS_BulkRaidData> members;
};

inline bool same_name(std::string_view left, std::string_view right) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
        [&](char a, char b) { return lower(a) == lower(b); });
}

inline std::optional<Snapshot> read(const PluginHostApi* host) {
    if (!HAILSTONE_HOST_HAS(host, get_raid_data_bulk)) return std::nullopt;

    // Leave room for append-only additions to the version 1 records.
    std::array<unsigned char, 16 * 1024> buffer{};
    const auto length = host->get_raid_data_bulk(host->host, buffer.data(), static_cast<int32_t>(buffer.size()));
    if (length < static_cast<int32_t>(sizeof(HS_BulkRaidHeader)) ||
        length > static_cast<int32_t>(buffer.size())) return std::nullopt;

    Snapshot snapshot;
    auto& header = snapshot.header;
    std::memcpy(&header, buffer.data(), sizeof(header));
    const auto size = static_cast<std::size_t>(length);
    if (header.version != 1 || header.size < sizeof(header) || header.size > size ||
        header.recordSize < sizeof(HS_BulkRaidData) || header.count != header.total ||
        header.count > MaxMembers || (header.active == 0 && header.count != 0)) return std::nullopt;
    const auto records_size = size - header.size;
    if (records_size / header.recordSize != header.count || records_size % header.recordSize != 0)
        return std::nullopt;

    snapshot.members.resize(header.count);
    const auto* record = buffer.data() + header.size;
    for (auto& member : snapshot.members) {
        std::memcpy(&member, record, sizeof(member));
        if (member.group < -1 || member.group > 11) return std::nullopt;
        record += header.recordSize;
    }
    return snapshot;
}
} // namespace bastion::raid
