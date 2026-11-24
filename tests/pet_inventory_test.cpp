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
Bytes status(unsigned flags = pi::ManagedPet, unsigned size = 88, unsigned managed = 1u << 13,
             unsigned occupied = 1u << 13) {
    Bytes b(size);
    put(b, 0, 1);
    put(b, 4, 4);
    put(b, 8, flags);
    if (flags & 2u)
        put(b, 12, 10000);
    text(b, 16, "Pet");
    if (size == 88) {
        put(b, 80, managed);
        put(b, 84, occupied);
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
void charmed_pet() {
    PetHost h;
    pi::Adapter a;
    a.set_host(&h.api);
    a.peer(true);
    a.game_state(GAMESTATE_INGAME);
    const unsigned flags = pi::ManagedPet | pi::Charmed;
    const auto receive_status = [&](unsigned state, unsigned managed, unsigned occupied) {
        const auto bytes = status(state, 88, managed, occupied);
        check(a.packet(pi::kOpcode, bytes.data(), bytes.size()), "pet status was not claimed");
    };
    const auto sent = [&](pi::Action expected) {
        hailstone::ByteReader reader(h.sent.back().bytes);
        reader.skip(4);
        return reader.le<std::uint32_t>() == static_cast<std::uint32_t>(expected);
    };
    const auto receive_item = [&] {
        const auto bytes = received_item();
        check(!a.packet(pi::kItemPacket, bytes.data(), bytes.size()), "pet item was swallowed");
    };
    constexpr unsigned primary = 1u << 13;
    check(a.command("open") && sent(pi::Action::Refresh), "charm inventory did not request a full snapshot");
    receive_status(flags, 0, 0);

    // A normal hand-in changes the NPC's inventory without sending an item to this window.
    h.now += 5000;
    a.pulse();
    check(sent(pi::Action::Status), "complete inventory did not use lightweight polling");
    receive_status(flags, 0, primary);
    h.held = item();
    check(!a.equip(a.revision(), 13, h.held->id, h.held->serial, h.held->instance_id, 1),
          "occupied slot with missing details could be overwritten");
    h.now += 5000;
    a.pulse();
    check(sent(pi::Action::Refresh), "status-only hand-in did not trigger a full refresh");
    receive_status(flags, 0, primary);
    receive_item();
    check(a.items().at(13).value.id == 200 && a.inspect(a.revision(), 13), "handed-in item was not inspectable");
    h.held.reset();
    check(!a.remove(a.revision(), 13, 200, h.received.serial, h.received.instance_id),
          "unmanaged hand-in could be retrieved");

    // A replacement can have exactly the same occupied and managed slot masks.
    for (int poll = 0; poll < 5; ++poll) {
        h.now += 5000;
        a.pulse();
        check(sent(pi::Action::Status), "charm sent needless full snapshots between polls");
        receive_status(flags, 0, primary);
    }
    h.now += 5000;
    a.pulse();
    check(sent(pi::Action::Refresh), "unchanged masks prevented periodic charm refresh");
    receive_status(flags, 0, primary);
    h.received = item(201);
    receive_item();
    check(a.items().at(13).value.id == 201, "same-slot hand-in replacement was not displayed");

    h.now += 1100;
    h.held = item();
    receive_status(flags | pi::Locked | pi::Casting, 0, primary);
    check(!a.equip(a.revision(), 13, 100, h.held->serial, 70, 1), "charm equip bypassed casting");
    receive_status(flags | pi::Locked, 0, primary);
    check(a.equip(a.revision(), 13, 100, h.held->serial, 70, 1) && sent(pi::Action::Equip),
          "charm equip blocked during removal cooldown");
    h.received = *h.held;
    receive_item();
    receive_status(flags | pi::Locked, primary, primary);
    h.held.reset();
    h.now += 1100;
    check(!a.remove(a.revision(), 13, 100, h.received.serial, 70), "charm remove bypassed cooldown");
    receive_status(flags, primary, primary);
    check(a.remove(a.revision(), 13, 100, h.received.serial, 70) && sent(pi::Action::Remove),
          "eligible charm item could not return to cursor");
    receive_status(flags, 0, 0);
    check(a.items().empty(), "empty status retained the old item");

    const auto requests = h.sent.size();
    check(a.command("") && a.command("") && !a.fresh() && h.sent.size() == requests,
          "reopen reused status or ignored the request throttle");
    h.now += 1100;
    a.pulse();
    check(h.sent.size() == requests + 1 && sent(pi::Action::Refresh), "reopen did not queue a full snapshot");
}
} // namespace
int main() {
    try {
        codec();
        adapter();
        charmed_pet();
        received_visibility<pi::Adapter>("pet_inventory");
        std::cout << "Pet inventory PASS " << checks << " checks\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
