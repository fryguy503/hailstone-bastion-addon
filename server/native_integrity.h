// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bastion_native_integrity {
constexpr std::uint32_t magic = 0x43495431, version = 3;
constexpr std::size_t report_size = 168;
constexpr std::uint64_t required_features = 0x3fff;
constexpr std::uint32_t lifetime_ms = 60000;
enum class Status : std::uint32_t { Accepted = 1, Rejected = 2, Unsupported = 3, Malformed = 4 };
struct Binary {
    std::uint64_t bytes = 0, fnv63 = 0;
    std::array<std::uint8_t, 32> sha{};
};
struct Report {
    std::uint64_t nonce = 0, features = 0;
    Binary executable, addon;
    std::string addon_version;
};
inline std::uint64_t read(std::span<const std::uint8_t> data, std::size_t at, unsigned width) {
    std::uint64_t value = 0;
    for (unsigned n = 0; n < width; ++n) value |= static_cast<std::uint64_t>(data[at + n]) << (n * 8);
    return value;
}
inline void write(std::vector<std::uint8_t>& data, std::uint64_t value, unsigned width) {
    for (unsigned n = 0; n < width; ++n) data.push_back(static_cast<std::uint8_t>(value >> (n * 8)));
}
inline bool is_native(std::span<const std::uint8_t> data) {
    return data.size() >= 8 && read(data, 0, 4) == magic && read(data, 4, 4) == version;
}
inline std::vector<std::uint8_t> header(unsigned kind, unsigned flags, std::uint64_t nonce) {
    std::vector<std::uint8_t> data;
    data.reserve(168);
    write(data, magic, 4); write(data, version, 4); write(data, kind, 4); write(data, flags, 4); write(data, nonce, 8);
    return data;
}
inline std::vector<std::uint8_t> challenge(std::uint64_t nonce) {
    auto data = header(1, 0, nonce); write(data, required_features, 8); return data;
}
inline std::vector<std::uint8_t> acknowledgement(std::uint64_t nonce, Status status, std::string_view reason) {
    auto data = header(3, static_cast<unsigned>(status), nonce);
    const auto length = std::min<std::size_t>(reason.size(), 127);
    for (std::size_t n = 0; n < 128; ++n) {
        const unsigned char c = n < length ? static_cast<unsigned char>(reason[n]) : 0;
        data.push_back(static_cast<std::uint8_t>(n < length ? (c >= 32 && c < 127 ? c : '?') : 0));
    }
    return data;
}
inline bool decode(std::span<const std::uint8_t> data, Report& out) {
    if (data.size() != report_size || !is_native(data) || read(data, 8, 4) != 2 ||
        read(data, 12, 4) || !read(data, 16, 8) || read(data, 32, 4) != 3 || read(data, 36, 4)) return false;
    Report value;
    value.nonce = read(data, 16, 8); value.features = read(data, 24, 8);
    const auto binary = [&](std::size_t at, Binary& target, std::uint64_t maximum) {
        target.bytes = read(data, at, 8); target.fnv63 = read(data, at + 8, 8);
        std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(at + 16), 32, target.sha.begin());
        return target.bytes && target.bytes <= maximum && target.fnv63 && target.fnv63 <= INT64_MAX &&
            std::any_of(target.sha.begin(), target.sha.end(), [](auto byte) { return byte != 0; });
    };
    if (!binary(40, value.executable, 512ull * 1024 * 1024) || !binary(88, value.addon, 128ull * 1024 * 1024)) return false;
    bool ended = false;
    for (std::size_t n = 136; n < report_size; ++n) {
        if (!data[n]) { ended = true; continue; }
        if (ended || data[n] < 32 || data[n] >= 127) return false;
        value.addon_version.push_back(static_cast<char>(data[n]));
    }
    if (!ended || value.addon_version.empty()) return false;
    out = std::move(value); return true;
}
inline std::string hex(const std::array<std::uint8_t, 32>& value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result; result.reserve(64);
    for (const auto byte : value) { result.push_back(digits[byte >> 4]); result.push_back(digits[byte & 15]); }
    return result;
}
inline bool approved(std::string_view setting, const Report& report) {
    const auto expected = hex(report.executable.sha) + ':' + hex(report.addon.sha);
    constexpr std::string_view delimiters = ",;| \t\r\n";
    for (std::size_t position = 0; position < setting.size();) {
        position = setting.find_first_not_of(delimiters, position);
        if (position == setting.npos) break;
        auto end = setting.find_first_of(delimiters, position);
        if (end == setting.npos) end = setting.size();
        const auto token = setting.substr(position, end - position);
        if (token.size() == expected.size() && std::equal(token.begin(), token.end(), expected.begin(), [](char a, char b) {
            if (a >= 'A' && a <= 'F') a = static_cast<char>(a + ('a' - 'A'));
            return a == b;
        })) return true;
        position = end;
    }
    return false;
}
// Per authenticated WORLD connection. A successful send is not acceptance;
// only a fresh, complete report matching an explicitly approved binary pair is.
class Session {
public:
    void issue(std::uint64_t nonce, std::uint32_t now, std::uint32_t account = 0) {
        nonce_ = nonce; issued_ = now; account_ = account; verified_ = false; accepted_.fill(0);
    }
    std::uint64_t nonce() const { return nonce_; }
    bool verified() const { return verified_; }
    bool fresh(std::uint32_t now) const { return nonce_ && static_cast<std::uint32_t>(now - issued_) < lifetime_ms; }
    Status evaluate(std::span<const std::uint8_t> data, std::uint32_t now, std::string_view allowlist, Report& report,
                    std::uint32_t account = 0) {
        if (!decode(data, report)) return Status::Malformed;
        if (account != account_ || report.nonce != nonce_ || !fresh(now)) return Status::Rejected;
        if ((report.features & required_features) != required_features) return Status::Unsupported;
        if (!approved(allowlist, report)) return Status::Rejected;
        if (verified_ && !std::equal(data.begin(), data.end(), accepted_.begin())) return Status::Rejected;
        std::copy(data.begin(), data.end(), accepted_.begin()); verified_ = true;
        return Status::Accepted;
    }
private:
    std::uint64_t nonce_ = 0;
    std::uint32_t issued_ = 0, account_ = 0;
    bool verified_ = false;
    std::array<std::uint8_t, report_size> accepted_{};
};
} // namespace bastion_native_integrity
