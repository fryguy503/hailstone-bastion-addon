// SPDX-License-Identifier: MIT
#pragma once

#include "hailstone_byte_io.h"
#include <algorithm>
#include <optional>

namespace bastion::tradeskill_plans {
inline constexpr std::uint16_t kOpcode = 0x7d29;
inline constexpr std::uint32_t kMagic = 0x4e4c5042;
inline constexpr std::size_t kHeader = 20;
inline constexpr std::size_t kFragment = 4000;
inline constexpr std::size_t kMaxBody = 262144;

struct Chunk {
    std::uint16_t status;
    std::uint32_t request, total, offset;
    std::string_view bytes;
};

inline std::vector<std::byte> encode(std::uint32_t request, std::uint16_t status,
                                    std::string_view body, std::size_t offset) {
    if (!request || body.empty() || body.size() > kMaxBody || offset >= body.size() || offset % kFragment)
        return {};
    const auto piece = body.substr(offset, kFragment);
    hailstone::ByteWriter writer;
    writer.reserve(kHeader + piece.size());
    writer.le(kMagic);
    writer.le<std::uint16_t>(1);
    writer.le(status);
    writer.le(request);
    writer.le(static_cast<std::uint32_t>(body.size()));
    writer.le(static_cast<std::uint32_t>(offset));
    writer.bytes({reinterpret_cast<const std::byte *>(piece.data()), piece.size()});
    return writer.take();
}

inline std::optional<Chunk> decode(const void *data, std::size_t size) {
    if (!data || size <= kHeader || size > kHeader + kFragment)
        return std::nullopt;
    hailstone::ByteReader reader(data, size);
    if (reader.le<std::uint32_t>() != kMagic || reader.le<std::uint16_t>() != 1)
        return std::nullopt;
    Chunk chunk{reader.le<std::uint16_t>(), reader.le<std::uint32_t>(), reader.le<std::uint32_t>(),
                reader.le<std::uint32_t>(), {}};
    if (!chunk.request || !chunk.total || chunk.total > kMaxBody || chunk.offset >= chunk.total ||
        chunk.offset % kFragment || reader.remaining() != std::min<std::size_t>(kFragment, chunk.total - chunk.offset))
        return std::nullopt;
    const auto piece = reader.bytes(reader.remaining());
    chunk.bytes = {reinterpret_cast<const char *>(piece.data()), piece.size()};
    return chunk;
}

struct Reply {
    bool append(const Chunk &chunk) {
        if (chunk.offset != body.size() || (chunk.offset && (chunk.total != total || chunk.status != status)))
            return false;
        if (!chunk.offset) {
            total = chunk.total;
            status = chunk.status;
            body.reserve(total);
        }
        body.append(chunk.bytes);
        return true;
    }
    bool complete() const { return total != 0 && total == body.size(); }
    std::uint32_t total = 0;
    std::uint16_t status = 0;
    std::string body;
};
} // namespace bastion::tradeskill_plans
