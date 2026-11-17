#include "telegraphs_protocol.h"
#include "hailstone_byte_io.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace bastion::telegraphs {
namespace {
template<class T> bool within(T value, T first, T last) { return value >= first && value <= last; }
bool finite(float value, float limit) { return std::isfinite(value) && std::abs(value) <= limit; }
}

std::int32_t distance(std::uint32_t first, std::uint32_t second) noexcept {
    return std::bit_cast<std::int32_t>(first - second);
}
bool newer(std::uint32_t candidate, std::uint32_t reference) noexcept { return distance(candidate, reference) > 0; }
bool reached(std::uint32_t now, std::uint32_t deadline) noexcept { return distance(now, deadline) >= 0; }

bool valid(const Packet& p) noexcept {
    if (p.magic != kMagic || p.version != kVersion || p.reserved != std::array<std::uint8_t, 3>{} ||
        (p.flags & ~kKnownFlags) || !within(p.action, Action::Create, Action::Presented) ||
        !within(p.shape, Shape::None, Shape::Target) || !within(p.anchor, Anchor::World, Anchor::Target) ||
        !within(p.disposition, Disposition::Auto, Disposition::Safe) ||
        !within(p.mechanic, Mechanic::None, Mechanic::Custom) || !within(p.style, Style::Auto, Style::Ring)) return false;
    for (float coordinate : {p.x, p.y, p.z, p.heading}) if (!finite(coordinate, 1000000)) return false;
    for (float dimension : {p.p1, p.p2, p.p3}) if (!finite(dimension, 10000)) return false;
    if (!finite(p.p4, 128) || !finite(p.p5, 1000)) return false;

    if (p.action != Action::Create && p.action != Action::Update) {
        if (p.shape != Shape::None || p.anchor != Anchor::World || p.flags ||
            p.disposition != Disposition::Auto || p.mechanic != Mechanic::None || p.style != Style::Auto ||
            p.owner_id || p.anchor_id || p.spell_id || p.impact_ms || p.expire_ms ||
            p.x != 0 || p.y != 0 || p.z != 0 || p.heading != 0 || p.p1 != 0 || p.p2 != 0 || p.p3 != 0 || p.p4 != 0 || p.p5 != 0 || p.color_argb) return false;
        switch (p.action) {
        case Action::Cancel: case Action::Presented: return p.id && !p.start_ms;
        case Action::ClearAll: return !p.id && !p.revision && !p.start_ms;
        case Action::SnapshotBegin: case Action::SnapshotEnd: return !p.id && !p.start_ms && p.revision <= kMaximumActive;
        case Action::Hello: return !p.generation && !p.id && p.revision <= 1 && !p.start_ms;
        case Action::HelloAck: return !p.id && !p.revision;
        default: return false;
        }
    }

    if (!p.id || p.shape == Shape::None || (p.anchor == Anchor::Owner && !p.owner_id) ||
        ((p.anchor == Anchor::Entity || p.anchor == Anchor::Target) && !p.anchor_id)) return false;
    if (p.p4 != 0 && (p.p4 < 8 || std::abs(p.p4 - std::floor(p.p4 + 0.5f)) > 0.001f)) return false;
    if (p.p1 <= 0) return false;
    switch (p.shape) {
    case Shape::Donut: if (p.p2 < 0 || p.p2 >= p.p1) return false; break;
    case Shape::Cone: if (p.p2 <= 0 || p.p2 > 360 || p.p3 < 0 || p.p3 >= p.p1) return false; break;
    case Shape::Rectangle: case Shape::Line: case Shape::Cross: if (p.p2 <= 0) return false; break;
    default: break;
    }
    const auto start = distance(p.start_ms, p.server_now_ms);
    const auto impact = distance(p.impact_ms, p.start_ms);
    const auto tail = distance(p.expire_ms, p.impact_ms);
    const auto life = distance(p.expire_ms, p.start_ms);
    return start >= -3600000 && start <= 600000 && impact > 0 && tail >= 0 && life > 0 && life <= 3600000;
}

std::optional<Packet> decode(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() != kPacketBytes) return std::nullopt;
    hailstone::ByteReader r(bytes);
    Packet p;
    p.magic = r.le<std::uint32_t>(); p.version = r.le<std::uint8_t>();
    p.action = static_cast<Action>(r.le<std::uint8_t>()); p.shape = static_cast<Shape>(r.le<std::uint8_t>());
    p.anchor = static_cast<Anchor>(r.le<std::uint8_t>()); p.flags = r.le<std::uint16_t>();
    p.disposition = static_cast<Disposition>(r.le<std::uint8_t>());
    p.mechanic = static_cast<Mechanic>(r.le<std::uint8_t>()); p.style = static_cast<Style>(r.le<std::uint8_t>());
    for (auto& value : p.reserved) value = r.le<std::uint8_t>();
    p.generation = r.le<std::uint32_t>(); p.id = r.le<std::uint32_t>(); p.revision = r.le<std::uint32_t>();
    p.owner_id = r.le<std::uint32_t>(); p.anchor_id = r.le<std::uint32_t>(); p.spell_id = r.le<std::uint32_t>();
    p.server_now_ms = r.le<std::uint32_t>(); p.start_ms = r.le<std::uint32_t>();
    p.impact_ms = r.le<std::uint32_t>(); p.expire_ms = r.le<std::uint32_t>();
    p.x = r.f32(); p.y = r.f32(); p.z = r.f32(); p.heading = r.f32();
    p.p1 = r.f32(); p.p2 = r.f32(); p.p3 = r.f32(); p.p4 = r.f32(); p.p5 = r.f32();
    p.color_argb = r.le<std::uint32_t>();
    return valid(p) ? std::optional(p) : std::nullopt;
}

std::optional<Bytes> encode(const Packet& p) {
    if (!valid(p)) return std::nullopt;
    hailstone::ByteWriter w;
    w.le(p.magic); w.le(p.version); w.le(static_cast<std::uint8_t>(p.action));
    w.le(static_cast<std::uint8_t>(p.shape)); w.le(static_cast<std::uint8_t>(p.anchor)); w.le(p.flags);
    w.le(static_cast<std::uint8_t>(p.disposition)); w.le(static_cast<std::uint8_t>(p.mechanic));
    w.le(static_cast<std::uint8_t>(p.style));
    for (auto value : p.reserved) w.le(value);
    for (auto value : {p.generation, p.id, p.revision, p.owner_id, p.anchor_id, p.spell_id,
                       p.server_now_ms, p.start_ms, p.impact_ms, p.expire_ms}) w.le(value);
    for (float value : {p.x, p.y, p.z, p.heading, p.p1, p.p2, p.p3, p.p4, p.p5}) w.f32(value);
    w.le(p.color_argb);
    Bytes output{};
    std::memcpy(output.data(), w.data().data(), output.size());
    return output;
}

} // namespace bastion::telegraphs
