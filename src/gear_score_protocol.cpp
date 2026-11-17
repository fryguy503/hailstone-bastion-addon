#include "gear_score_protocol.h"
#include "hailstone_byte_io.h"

namespace bastion::gear_score {
bool valid(const Packet& p) {
    if (!p.session || (p.flags & ~127u) || p.key.reference || p.base > kMaximumMilli ||
        p.configuration > kMaximumMilli || p.equipped > kMaximumMilli || p.owned > kMaximumMilli) return false;
    const bool key_empty = p.key == Key{};
    const bool values_empty = !p.base && !p.configuration && !p.equipped && !p.owned;
    switch (p.action) {
    case Action::Hello: return !p.flags && !p.generation && !p.revision && key_empty && values_empty;
    case Action::HelloAck: case Action::Reset: return p.generation && !p.flags && key_empty && values_empty;
    case Action::Item:
        return p.generation && p.key.item && !p.equipped && !p.owned &&
            !(p.flags & (EquippedValid | OwnedValid | OwnedStale)) &&
            (!(p.flags & ConfigurationValid) || (p.flags & BaseValid)) &&
            (!(p.flags & NotGear) || (!p.base && !p.configuration));
    case Action::Summary:
        return p.generation && key_empty && !p.base && !p.configuration &&
            !(p.flags & (BaseValid | ConfigurationValid | NotGear)) &&
            (!(p.flags & OwnedStale) || (p.flags & OwnedValid));
    }
    return false;
}

std::optional<Packet> decode(std::span<const std::byte> bytes) {
    if (bytes.size() != kPacketBytes) return std::nullopt;
    hailstone::ByteReader reader(bytes);
    if (reader.le<std::uint32_t>() != kMagic || reader.le<std::uint8_t>() != 1) return std::nullopt;
    Packet p;
    p.action = static_cast<Action>(reader.le<std::uint8_t>());
    p.flags = reader.le<std::uint16_t>();
    p.session = reader.le<std::uint32_t>();
    p.generation = reader.le<std::uint32_t>();
    p.revision = reader.le<std::uint32_t>();
    p.key.item = reader.le<std::uint32_t>();
    for (auto& id : p.key.augments) id = reader.le<std::uint32_t>();
    p.key.reference = reader.le<std::uint32_t>();
    p.base = reader.le<std::uint32_t>();
    p.configuration = reader.le<std::uint32_t>();
    p.equipped = reader.le<std::uint32_t>();
    p.owned = reader.le<std::uint32_t>();
    return valid(p) ? std::optional(p) : std::nullopt;
}

std::vector<std::byte> hello(std::uint32_t session) {
    if (!session) return {};
    hailstone::ByteWriter writer;
    writer.le(kMagic);
    writer.le<std::uint8_t>(1);
    writer.le(static_cast<std::uint8_t>(Action::Hello));
    writer.le<std::uint16_t>(0);
    writer.le(session);
    for (unsigned i = 0; i < 14; ++i) writer.le<std::uint32_t>(0);
    return writer.take();
}
} // namespace bastion::gear_score
