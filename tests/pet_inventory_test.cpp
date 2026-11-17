#include "pet_inventory_addon.h"
#include "citadel_test_host.h"
#include "received_tools_test_host.h"
#include <iostream>
namespace pi = bastion::pet_inventory;
using namespace citadel_test;
namespace {
HailstoneItem item(std::uint32_t id = 100, const char *serial = "ABCDEF0123456789", std::uint32_t count = 1) {
    HailstoneItem it{};
    it.size = sizeof(it);
    it.id = id;
    it.stack_count = count;
    it.instance_id = 70;
    std::memcpy(it.name, "Fixture Item", sizeof("Fixture Item"));
    check(std::strlen(serial) < sizeof(it.serial), "fixture serial too long");
    std::memcpy(it.serial, serial, std::strlen(serial) + 1);
    return it;
}
struct PetHost : Host {
    std::optional<HailstoneItem> held;
    HailstoneItem received = item(200);
    std::uint32_t received_slot = 13;
    unsigned inspected = 0;
    std::function<void()> during_get;
    PetHost() {
        pet = 8;
        api.get_item = [](void *h, const HailstoneItemSlot *slot, HailstoneItem *out) {
            auto &s = *static_cast<PetHost *>(h);
            check(slot->slot == 33 && slot->type == 0, "noncursor lookup");
            if (s.during_get)
                s.during_get();
            if (!s.held)
                return 0;
            *out = *s.held;
            return 1;
        };
        api.decode_item_packet = [](void *h, const void *, std::size_t, HailstoneItemSlot *slot, HailstoneItem *out) {
            auto &s = *static_cast<PetHost *>(h);
            *slot = {0, static_cast<std::int16_t>(1800 + s.received_slot), -1, -1};
            *out = s.received;
            return 1;
        };
        api.inspect_item_packet = [](void *h, const void *, std::size_t) {
            ++static_cast<PetHost *>(h)->inspected;
            return 1;
        };
    }
};
Bytes status(unsigned flags = 1, unsigned size = 88) {
    Bytes b(size);
    put(b, 0, 1);
    put(b, 4, 4);
    put(b, 8, flags);
    if (flags & 2u)
        put(b, 12, 10000);
    text(b, 16, "Pet");
    if (size == 88) {
        put(b, 80, 1u << 13);
        put(b, 84, 1u << 13);
    }
    return b;
}
Bytes deletion(unsigned slot = 13) {
    Bytes b(28);
    put(b, 4, 1800 + slot, 2);
    put(b, 6, 65535, 2);
    put(b, 8, 65535, 2);
    put(b, 24, UINT32_MAX);
    return b;
}
Bytes received_item() {
    Bytes b(8);
    put(b, 0, 0x67);
    return b;
}
void codec() {
    auto b = status();
    check(pi::decode(b)->ownership_known, "extended status");
    check(!pi::decode(status(1, 80))->ownership_known, "legacy fabricated ownership");
    for (std::size_t n = 0; n < 88; ++n)
        if (n != 80)
            check(!pi::decode(std::span(b).first(n)), "truncated status");
    for (std::size_t at : {std::size_t(0), std::size_t(4), std::size_t(8), std::size_t(80)}) {
        auto bad = b;
        put(bad, at, UINT32_MAX);
        check(!pi::decode(bad), "invalid status");
    }
    auto bad = b;
    put(bad, 84, 0);
    check(!pi::decode(bad), "managed without occupied");
    bad = b;
    bad[16] = std::byte{1};
    check(!pi::decode(bad), "unsafe status text");
    for (auto slot : pi::kSlots) {
        auto move = pi::request(pi::Action::Equip, slot, 3);
        check(move.size() == 28, "move size");
        hailstone::ByteReader r(move);
        check(r.le<std::uint32_t>() == 1 && r.le<std::uint32_t>() == 1, "move version/action");
        check(r.le<std::int16_t>() == 0 && r.le<std::int16_t>() == 0 && r.le<std::int16_t>() == 33 &&
                  r.le<std::int16_t>() == -1 && r.le<std::int16_t>() == -1 && r.le<std::int16_t>() == 0 &&
                  r.le<std::uint32_t>() == slot && r.le<std::uint32_t>() == 3,
              "move literal fields");
        check(pi::deleted_slot(deletion(slot)) == slot, "virtual delete slot");
    }
    check(pi::request(pi::Action::Equip, 4, 1).empty() && pi::request(pi::Action::Remove, 13, 0).empty() &&
              pi::request(pi::Action::Refresh, 13, 0).empty(),
          "unsupported request");
    check(!pi::deleted_slot(deletion(4)), "unsupported virtual slot");
    auto del = deletion();
    put(del, 24, 1);
    check(!pi::deleted_slot(del), "partial delete fabricated empty slot");
}
void adapter() {
    PetHost h;
    pi::Adapter a;
    a.set_host(&h.api);
    a.initialize();
    a.peer(true);
    a.game_state(GAMESTATE_INGAME);
    check(a.command("open") && a.pending(), "manual refresh");
    auto st = status();
    a.packet(pi::kOpcode, st.data(), st.size());
    check(a.fresh() && !a.pending(), "fresh status");
    auto packet = received_item();
    check(!a.packet(pi::kItemPacket, packet.data(), packet.size()) && a.items().size() == 1,
          "ordinary item not observed or swallowed");
    h.received = item(200, "");
    a.packet(pi::kItemPacket, packet.data(), packet.size());
    check(a.items().size() == 1 && a.items().at(13).value.id == 200, "unknown serial hid received presentation");
    check(!a.remove(a.revision(), 13, 200, "", 70), "missing serial allowed item removal");
    h.received = item(200);
    a.packet(pi::kItemPacket, packet.data(), packet.size());
    const auto old = a.revision();
    h.held = item(100, "ABCDEF0123456789", 3);
    h.now += 1100;
    check(!a.equip(old, 13, 100, "different", 70, 3) && !a.equip(old, 13, 100, "ABCDEF0123456789", 70, 2),
          "cursor fingerprint not checked");
    st = status(3);
    a.packet(pi::kOpcode, st.data(), st.size());
    check(a.equip(a.revision(), 13, 100, "ABCDEF0123456789", 70, 3), "cooldown wrongly blocked equip");
    hailstone::ByteReader move(h.sent.back().bytes);
    move.skip(4);
    check(move.le<std::uint32_t>() == 1, "equip opcode action");
    check(!a.equip(a.revision(), 13, 100, "ABCDEF0123456789", 70, 3), "duplicate inflight equip");
    a.packet(pi::kOpcode, st.data(), st.size());
    h.held.reset();
    h.now += 1100;
    check(!a.remove(a.revision(), 13, 200, "ABCDEF0123456789", 70), "locked remove");
    st = status();
    a.packet(pi::kOpcode, st.data(), st.size());
    h.held = item(999, "");
    check(!a.remove(a.revision(), 13, 200, "ABCDEF0123456789", 70), "unknown occupied cursor treated empty");
    h.held.reset();
    check(a.inspect(a.revision(), 13) && h.inspected == 1, "received local inspect");
    check(a.remove(a.revision(), 13, 200, "ABCDEF0123456789", 70), "manual remove refused");
    move = hailstone::ByteReader(h.sent.back().bytes);
    move.skip(4);
    check(move.le<std::uint32_t>() == 3, "remove action");
    a.packet(pi::kOpcode, st.data(), st.size());
    const auto del = deletion();
    check(!a.packet(pi::kDeleteItem, del.data(), del.size()) && a.items().empty(),
          "ordinary delete swallowed or stale row retained");
    a.packet(pi::kItemPacket, packet.data(), packet.size());
    h.now += 1100;
    h.during_get = [&] { a.begin_zone(); };
    check(!a.remove(a.revision(), 13, 200, "ABCDEF0123456789", 70), "read callback retirement ignored");
    h.during_get = {};
    a.game_state(GAMESTATE_INGAME);
    a.command("open");
    a.packet(pi::kOpcode, st.data(), st.size());
    a.packet(pi::kItemPacket, packet.data(), packet.size());
    ++h.pet_incarnation;
    a.pulse();
    check(a.items().empty() && !a.fresh(), "pet incarnation reused items/status");
    h.now += 11000;
    a.pulse();
    check(!a.fresh(), "timeout retained readiness");
    a.peer(false);
    check(!a.packet(pi::kOpcode, nullptr, 0), "nonpeer consumed");
    a.shutdown();
    check(!h.functions.contains("pet_inventory"), "UI leaked");
}
} // namespace
int main() {
    try {
        codec();
        adapter();
        received_visibility<pi::Adapter>("pet_inventory");
        std::cout << "Pet inventory PASS " << checks << " checks\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
