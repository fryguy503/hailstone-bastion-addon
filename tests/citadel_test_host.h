#pragma once
#include "hailstone_plugin.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace citadel_test {
inline unsigned checks = 0;
inline void check(bool value, const char *why) {
    ++checks;
    if (!value)
        throw std::runtime_error(why);
}
using Bytes = std::vector<std::byte>;
inline void put(Bytes &b, std::size_t at, std::uint64_t value, unsigned width = 4) {
    for (unsigned i = 0; i < width; ++i)
        b.at(at + i) = std::byte(static_cast<std::uint8_t>(value >> (i * 8)));
}
inline void text(Bytes &b, std::size_t at, std::string_view value) {
    for (char c : value)
        b.at(at++) = std::byte(static_cast<unsigned char>(c));
}
struct Host {
    struct Sent {
        std::uint16_t opcode;
        Bytes bytes;
    };
    PluginHostApi api{};
    std::uint64_t now = 1000, incarnation = 10, pet_incarnation = 20;
    std::uint32_t player = 7, zone = 2, pet = 0;
    bool in_zone = true, admit = true, prompt_safe = false;
    std::vector<Sent> sent;
    std::vector<std::string> commands;
    std::function<void()> during_send;
    struct Function {
        HailstoneUiFn fn;
        void *self;
    };
    std::map<std::string, Function> functions;
    Host() {
        api.size = sizeof(api);
        api.host = this;
        api.do_command = [](void *h, const char *line) { static_cast<Host *>(h)->commands.emplace_back(line); };
        api.monotonic_ms = [](void *h) { return static_cast<Host *>(h)->now; };
        api.get_player = [](void *h, HailstonePlayer *out) {
            auto &s = *static_cast<Host *>(h);
            out->size = sizeof(*out);
            out->spawn_id = s.player;
            out->zone_id = s.zone;
            out->pet_id = s.pet;
            out->in_zone = s.in_zone ? 1 : 0;
#ifdef HAILSTONE_PLAYER_PROMPT_SAFE
            out->observation_flags = s.prompt_safe ? HAILSTONE_PLAYER_PROMPT_SAFE : 0;
#endif

            std::memcpy(out->name, "Fixture", sizeof("Fixture"));
            return 1;
        };
        api.get_spawn = [](void *h, std::uint32_t id, HailstoneSpawn *out) {
            auto &s = *static_cast<Host *>(h);
            out->size = sizeof(*out);
            out->id = id;
            out->incarnation = id == s.player ? s.incarnation : s.pet_incarnation;
            return 1;
        };
        api.send_packet = [](void *h, int, std::uint16_t opcode, const void *bytes, std::size_t size) {
            auto &s = *static_cast<Host *>(h);
            s.sent.push_back(
                {opcode, Bytes(static_cast<const std::byte *>(bytes), static_cast<const std::byte *>(bytes) + size)});
            if (s.during_send)
                s.during_send();
            return s.admit ? 1 : 0;
        };
        api.add_ui_function = [](void *h, const char *name, HailstoneUiFn fn, void *self) {
            static_cast<Host *>(h)->functions[name] = {fn, self};
            return 1;
        };
        api.remove_ui_function = [](void *h, const char *name) { static_cast<Host *>(h)->functions.erase(name); };
    }
};
} // namespace citadel_test
