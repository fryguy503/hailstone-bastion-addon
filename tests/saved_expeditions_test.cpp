#include "saved_expeditions_addon.h"
#include "citadel_test_host.h"
#include "received_tools_test_host.h"
#include <iostream>
namespace dz = bastion::saved_expeditions;
using namespace citadel_test;
namespace {
const std::string uuid = "01234567-89ab-cdef-0123-456789abcdef";
Bytes response(std::uint32_t id, unsigned action = 3, unsigned result = 0, unsigned count = 1) {
    Bytes b(160 + 272 * count);
    put(b, 0, 0x31525a44);
    put(b, 4, 1, 2);
    put(b, 6, action, 2);
    put(b, 8, id);
    put(b, 12, result);
    put(b, 16, 10000, 8);
    put(b, 24, count);
    for (unsigned i = 0; i < count; ++i) {
        const auto base = 160 + 272 * std::size_t(i);
        put(b, base, 50 + i);
        put(b, base + 4, 9);
        put(b, base + 12, 22);
        put(b, base + 16, 3);
        put(b, base + 20, 6);
        put(b, base + 24, 900);
        put(b, base + 32, 11000, 8);
        text(b, base + 40, uuid);
        b[base + 40] = std::byte('0' + i % 10);
        text(b, base + 80, "Saved Fixture");
        text(b, base + 208, "Leader");
    }
    return b;
}
std::uint32_t sent_id(const Host &host) {
    hailstone::ByteReader r(host.sent.back().bytes);
    r.skip(8);
    return r.le<std::uint32_t>();
}
void codec() {
    const auto good = response(3);
    const auto value = dz::decode(good);
    check(value && value->entries.size() == 1 && value->entries[0].leader_id == 900 &&
              value->entries[0].instance_id == 22,
          "valid literal fields");
    for (std::size_t size = 0; size < good.size(); ++size)
        check(!dz::decode(std::span(good).first(size)), "truncation accepted");
    auto bad = good;
    bad.push_back(std::byte{});
    check(!dz::decode(bad), "trailing bytes");
    for (std::size_t at : {std::size_t(0), std::size_t(4), std::size_t(6), std::size_t(12), std::size_t(28)}) {
        bad = good;
        put(bad, at, 0xffffffff);
        check(!dz::decode(bad), "invalid field");
    }
    for (std::size_t at : {std::size_t(8), std::size_t(160), std::size_t(164)}) {
        bad = good;
        put(bad, at, 0);
        check(!dz::decode(bad), "required identity");
    }
    for (std::size_t at : {std::size_t(208), std::size_t(236), std::size_t(239)}) {
        bad = good;
        bad[at] = std::byte{0xff};
        check(!dz::decode(bad), "invalid uuid");
    }
    bad = response(1, 3, 0, 2);
    put(bad, 432, 50);
    check(!dz::decode(bad), "duplicate dz");
    bad = response(1, 3, 0, 2);
    text(bad, 472, uuid);
    check(!dz::decode(bad), "duplicate uuid");
    check(!dz::decode(response(1, 3, 0, 17)), "unbounded count");
    for (unsigned result = 0; result <= 15; ++result)
        check(dz::decode(response(1, 4, result, 0)).has_value(), "valid result");
    dz::Entry e;
    e.dz_id = 50;
    e.uuid = uuid;
    const auto request = dz::request(dz::Action::Resume, 12, &e);
    check(request.size() == 64 && std::to_integer<unsigned>(request[6]) == 2 &&
              std::to_integer<unsigned>(request[12]) == 50,
          "resume encoding");
    for (std::size_t i = 56; i < 64; ++i)
        check(request[i] == std::byte{}, "reserved byte changed");
    check(dz::request(dz::Action::List, 0).empty() && dz::request(dz::Action::Resume, 1).empty(), "invalid request");
}
void adapter() {
    Host h;
    dz::Adapter a;
    a.set_host(&h.api);
    a.initialize();
    check(h.functions.contains("saved_expeditions"), "UI registration");
    check(!a.command("open") && h.sent.empty(), "inactive sent request");
    a.peer(true);
    a.game_state(GAMESTATE_INGAME);
    for (const auto *text : {"add MiXeDCaSe", "remove AnotherName", "list", "saved", "resume ABC-def"}) {
        check(a.command(text), "server shortcut rejected");
        check(h.commands.back() == std::string("/say #dzmanager ") + text, "server shortcut altered arguments");
    }
    check(h.sent.empty(), "server shortcut emitted DZR1 list");
    const auto commands = h.commands.size();
    check(!a.command("add Name\n/say injected") && h.commands.size() == commands, "multiline shortcut accepted");
    check(a.command("open") && h.sent.size() == 1 && a.pending(), "explicit open did not reserve");
    const auto id = sent_id(h);
    check(!a.refresh() && h.sent.size() == 1, "inflight duplicate");
    auto b = response(id + 1);
    a.packet(dz::kOpcode, b.data(), b.size());
    check(a.pending() && a.entries().empty(), "unmatched reply");
    b = response(id);
    a.packet(dz::kOpcode, b.data(), b.size());
    check(a.fresh() && a.entries().size() == 1, "matching list");
    const auto revision = a.revision();
    check(!a.resume(revision, 50, uuid), "rate gate missing");
    h.now += 1100;
    check(!a.resume(revision, 50, "bad") && !a.resume(revision + 1, 50, uuid), "stale selection");
    check(a.resume(revision, 50, uuid) && a.pending(), "manual rejoin refused");
    const auto resume_id = sent_id(h);
    b = response(resume_id, 4, 0, 0);
    a.packet(dz::kOpcode, b.data(), b.size());
    check(!a.pending() && !a.fresh(), "rejoin inferred fresh list");
    h.now += 1100;
    check(a.refresh(), "refresh refused");
    b = response(sent_id(h));
    a.packet(dz::kOpcode, b.data(), b.size());
    h.now += 60000;
    a.pulse();
    check(!a.fresh() && !a.resume(a.revision(), 50, uuid), "expired list usable");
    check(a.refresh(), "refresh after expiry");
    const auto old = sent_id(h);
    h.now += 10000;
    a.pulse();
    check(!a.pending(), "timeout");
    b = response(old);
    a.packet(dz::kOpcode, b.data(), b.size());
    check(!a.fresh(), "late reply admitted");
    check(a.refresh(), "new request");
    b = response(sent_id(h));
    ++h.incarnation;
    a.packet(dz::kOpcode, b.data(), b.size());
    check(a.entries().empty() && !a.pending(), "identity reuse admitted");
    check(a.refresh(), "new identity request");
    h.during_send = [&] { a.begin_zone(); };
    h.now += 1100; // an already-pending request must still be single flight
    a.begin_zone();
    a.game_state(GAMESTATE_INGAME);
    check(a.refresh() && !a.pending(), "send reentry retirement overwritten");
    h.during_send = {};
    a.peer(false);
    check(!a.packet(dz::kOpcode, nullptr, 0) && !a.refresh(), "wrong peer consumed");
    a.shutdown();
    check(!h.functions.contains("saved_expeditions"), "UI function leaked");
    dz::Adapter replacement;
    replacement.set_host(&h.api);
    replacement.peer(true);
    replacement.game_state(GAMESTATE_INGAME);
    check(replacement.refresh() && sent_id(h) != old, "replacement reused request id");
}
} // namespace
int main() {
    try {
        codec();
        adapter();
        received_visibility<dz::Adapter>("saved_expeditions");
        std::cout << "Saved expeditions PASS " << checks << " checks\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
