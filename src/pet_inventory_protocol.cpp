#include "pet_inventory_protocol.h"
#include "citadel_services.h"
namespace bastion::pet_inventory {
bool supported(std::uint32_t value) { return std::find(kSlots.begin(), kSlots.end(), value) != kSlots.end(); }
const char *slot_name(std::uint32_t slot) {
    static constexpr const char *names[] = {"Charm",     "Ear",     "Head",      "Face",     "Ear 2",    "Neck",
                                            "Shoulders", "Arms",    "Back",      "Wrist",    "Wrist 2",  "Range",
                                            "Hands",     "Primary", "Secondary", "Finger 1", "Finger 2", "Chest",
                                            "Legs",      "Feet",    "Waist"};
    return slot < 21 ? names[slot] : "Unknown";
}
std::optional<Status> decode(std::span<const std::byte> bytes) {
    if (bytes.size() != 80 && bytes.size() != 88)
        return std::nullopt;
    try {
        hailstone::ByteReader r(bytes);
        if (r.le<std::uint32_t>() != 1 || r.le<std::uint32_t>() != 4)
            return std::nullopt;
        Status s;
        s.flags = r.le<std::uint32_t>();
        s.remaining_ms = r.le<std::uint32_t>();
        s.name = services::text(r, 64);
        if ((s.flags & ~15u) || (!(s.flags & 1u) && (s.flags & (2u | 8u))) || (!(s.flags & 2u) && s.remaining_ms))
            return std::nullopt;
        if (bytes.size() == 88) {
            s.ownership_known = true;
            s.managed = r.le<std::uint32_t>();
            s.occupied = r.le<std::uint32_t>();
            std::uint32_t allowed = 0;
            for (auto slot : kSlots)
                allowed |= 1u << slot;
            if ((s.managed & ~s.occupied) || ((s.managed | s.occupied) & ~allowed) ||
                (!(s.flags & 1u) && (s.managed || s.occupied)))
                return std::nullopt;
        }
        return s;
    } catch (const std::out_of_range &) {
        return std::nullopt;
    }
}
std::optional<std::uint32_t> deleted_slot(std::span<const std::byte> bytes) {
    if (bytes.size() != 28)
        return std::nullopt;
    try {
        hailstone::ByteReader r(bytes);
        const auto type = r.le<std::int16_t>();
        r.skip(2);
        const auto slot = r.le<std::int16_t>();
        const auto sub = r.le<std::int16_t>(), aug = r.le<std::int16_t>();
        r.skip(14);
        if (type || slot < 1800 || sub != -1 || aug != -1 || r.le<std::uint32_t>() != UINT32_MAX ||
            !supported(static_cast<std::uint32_t>(slot - 1800)))
            return std::nullopt;
        return static_cast<std::uint32_t>(slot - 1800);
    } catch (const std::out_of_range &) {
        return std::nullopt;
    }
}
std::vector<std::byte> request(Action action, std::uint32_t slot, std::uint32_t count) {
    if (action < Action::Equip || action > Action::Status ||
        (action == Action::Equip || action == Action::Remove ? (!supported(slot) || !count) : (slot || count)))
        return {};
    hailstone::ByteWriter w;
    w.le<std::uint32_t>(1);
    w.le(static_cast<std::uint32_t>(action));
    w.le<std::int16_t>(0);
    w.le<std::int16_t>(0);
    w.le<std::int16_t>(33);
    w.le<std::int16_t>(-1);
    w.le<std::int16_t>(-1);
    w.le<std::int16_t>(0);
    w.le(slot);
    w.le(count);
    return w.take();
}
} // namespace bastion::pet_inventory
