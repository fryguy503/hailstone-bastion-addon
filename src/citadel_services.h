#pragma once
#include "hailstone_plugin.h"
#include "hailstone_byte_io.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace bastion::services {
// Registered SDK callbacks are C boundaries. Even a throwing host callback or
// a failed allocation must become a failed UI call rather than unwind through Lua.
template <class Callback> int guarded_ui(const PluginHostApi *api, Callback &&callback) noexcept {
    const auto log = [api](const char *message) noexcept {
        try {
            if (HAILSTONE_HOST_HAS(api, log)) api->log(api->host, 3, message);
        } catch (...) {
            // Error reporting must not reopen the exception boundary.
        }
    };
    try {
        return callback();
    } catch (const std::exception &error) {
        log(error.what());
    } catch (...) {
        log("Bastion UI callback failed");
    }
    return 0;
}
// Process-wide, non-wrapping identities. Zero means exhausted/unavailable.
inline std::uint32_t request_id() {
    static std::atomic<std::uint32_t> value{0};
    auto old = value.load(std::memory_order_relaxed);
    while (old != UINT32_MAX) {
        if (value.compare_exchange_weak(old, old + 1, std::memory_order_relaxed))
            return old + 1;
    }
    return 0;
}
inline std::uint64_t token() {
    static std::atomic<std::uint64_t> value{0};
    auto old = value.load(std::memory_order_relaxed);
    while (old != UINT64_MAX) {
        if (value.compare_exchange_weak(old, old + 1, std::memory_order_relaxed))
            return old + 1;
    }
    return 0;
}
inline std::string text(hailstone::ByteReader &r, std::size_t width, bool required = false) {
    auto bytes = r.bytes(width);
    const auto end = std::find(bytes.begin(), bytes.end(), std::byte{});
    if (end == bytes.end() || (required && end == bytes.begin()))
        throw std::out_of_range("text");
    std::string result;
    for (auto p = bytes.begin(); p != end; ++p) {
        const auto c = std::to_integer<unsigned char>(*p);
        if (c < 32 || c == 127)
            throw std::out_of_range("control text");
        result.push_back(static_cast<char>(c));
    }
    return result;
}
template <std::size_t N> std::string bounded(const char (&s)[N]) { return {s, std::find(s, s + N, '\0')}; }
struct Context {
    std::uint32_t player = 0, zone = 0;
    std::uint64_t incarnation = 0;
    std::string name;
    bool operator==(const Context &) const = default;
};
inline std::optional<Context> context(const PluginHostApi *api, bool selected, bool in_game) {
    if (!selected || !in_game || !HAILSTONE_HOST_HAS(api, get_player) || !HAILSTONE_HOST_HAS(api, get_spawn) ||
        !HAILSTONE_HOST_HAS(api, monotonic_ms) || !HAILSTONE_HOST_HAS(api, send_packet))
        return std::nullopt;
    HailstonePlayer player{};
    player.size = sizeof(player);
    if (!api->get_player(api->host, &player) ||
        player.size < offsetof(HailstonePlayer, in_zone) + sizeof(player.in_zone) || !player.in_zone ||
        !player.spawn_id)
        return std::nullopt;
    HailstoneSpawn spawn{};
    spawn.size = sizeof(spawn);
    if (!api->get_spawn(api->host, player.spawn_id, &spawn) ||
        spawn.size < offsetof(HailstoneSpawn, incarnation) + sizeof(spawn.incarnation) || !spawn.incarnation)
        return std::nullopt;
    return Context{player.spawn_id, player.zone_id, spawn.incarnation, bounded(player.name)};
}
inline std::optional<std::string> argument(const PluginHostApi *api, const HailstoneArgs *args, std::size_t index,
                                           std::size_t maximum = 96) {
    if (!HAILSTONE_HOST_HAS(api, arg))
        return std::nullopt;
    HailstoneValue value{};
    if (!api->arg(api->host, args, index, &value) || value.kind != HAILSTONE_STRING || !value.s)
        return std::nullopt;
    std::size_t n = 0;
    while (n <= maximum && value.s[n])
        ++n;
    if (n > maximum)
        return std::nullopt;
    return std::string(value.s, n);
}
inline std::optional<std::uint64_t> decimal(const PluginHostApi *api, const HailstoneArgs *args, std::size_t index) {
    const auto value = argument(api, args, index, 20);
    if (!value || value->empty())
        return std::nullopt;
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value->data(), value->data() + value->size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value->data() + value->size())
        return std::nullopt;
    return result;
}
struct Output {
    const PluginHostApi *host;
    HailstoneOut *out;
    bool valid() const {
        return out && HAILSTONE_HOST_HAS(host, out_begin_table) && HAILSTONE_HOST_HAS(host, out_begin_array) &&
               HAILSTONE_HOST_HAS(host, out_key) && HAILSTONE_HOST_HAS(host, out_value) &&
               HAILSTONE_HOST_HAS(host, out_end);
    }
    void begin() const { host->out_begin_table(host->host, out); }
    void end() const { host->out_end(host->host, out); }
    void key(const char *name) const { host->out_key(host->host, out, name); }
    void array(const char *name) const {
        key(name);
        host->out_begin_array(host->host, out);
    }
    void boolean(const char *name, bool value) const {
        key(name);
        HailstoneValue v{};
        v.kind = HAILSTONE_BOOL;
        v.i = value ? 1 : 0;
        host->out_value(host->host, out, &v);
    }
    void number(const char *name, std::int64_t value) const {
        key(name);
        HailstoneValue v{};
        v.kind = HAILSTONE_INT;
        v.i = value;
        host->out_value(host->host, out, &v);
    }
    void string(const char *name, std::string_view value) const {
        key(name);
        const std::string copy(value);
        HailstoneValue v{};
        v.kind = HAILSTONE_STRING;
        v.s = copy.c_str();
        host->out_value(host->host, out, &v);
    }
    void identity(const char *name, std::uint64_t value) const { string(name, std::to_string(value)); }
};
} // namespace bastion::services
