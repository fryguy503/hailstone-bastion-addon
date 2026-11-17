#include "home_city_addon.h"
#include "citadel_test_host.h"
#include "received_tools_test_host.h"
#include <iostream>
namespace hc = bastion::home_city;
using namespace citadel_test;
namespace {
Bytes response(std::uint32_t id, unsigned action = 2, unsigned result = 0, unsigned count = 1) {
    Bytes b(216 + 72 * count);
    put(b, 0, 0x31594348);
    put(b, 4, 1, 2);
    put(b, 6, action, 2);
    put(b, 8, id);
    put(b, 12, result);
    put(b, 16, UINT64_C(0xfedcba9876543210), 8);
    put(b, 24, UINT64_C(0xeedcba9876543210), 8);
    put(b, 32, 1);
    put(b, 36, 2);
    put(b, 40, 3);
    put(b, 44, 4);
    put(b, 48, count);
    for (unsigned i = 0; i < count; ++i) {
        const auto base = 216 + 72 * std::size_t(i);
        put(b, base, 10 + i);
        put(b, base + 4, 20 + i);
        text(b, base + 8, "Destination");
    }
    return b;
}
std::uint32_t id(const Host &h) {
    hailstone::ByteReader r(h.sent.back().bytes);
    r.skip(8);
    return r.le<std::uint32_t>();
}
void codec() {
    const auto good = response(7);
    const auto parsed = hc::decode(good);
    check(parsed && parsed->entries.size() == 1 && parsed->grant == UINT64_C(0xfedcba9876543210),
          "literal full-width grant");
    for (std::size_t i = 0; i < good.size(); ++i)
        check(!hc::decode(std::span(good).first(i)), "truncation");
    auto bad = good;
    bad.push_back(std::byte{});
    check(!hc::decode(bad), "trailing bytes");
    for (std::size_t at : {std::size_t(0), std::size_t(4), std::size_t(6), std::size_t(12), std::size_t(52)}) {
        bad = good;
        put(bad, at, 0xffffffff);
        check(!hc::decode(bad), "invalid envelope");
    }
    bad = response(1, 2, 0, 2);
    put(bad, 288, 10);
    check(!hc::decode(bad), "duplicate choices");
    bad = response(1, 3, 0, 1);
    check(!hc::decode(bad), "commit with choices");
    bad = response(1, 2, 0, 33);
    check(!hc::decode(bad), "choice bound");
    bad = good;
    bad[56] = std::byte{1};
    check(!hc::decode(bad), "unsafe plain text");
    for (unsigned result = 0; result <= 7; ++result)
        check(hc::decode(response(1, 3, result, 0)).has_value(), "valid terminal result");
    const auto request =
        hc::request(hc::Action::Commit, 5, 10, UINT64_C(0xfedcba9876543210), UINT64_C(0xeedcba9876543210));
    check(request.size() == 32 && request[6] == std::byte{3} && request[12] == std::byte{10}, "request offsets");
    check(hc::request(hc::Action::List, 1, 3).empty() && hc::request(hc::Action::Commit, 1).empty() &&
              hc::request(hc::Action::Suppress, 1, 2, 3, 4).empty(),
          "invalid request emitted");
}
void adapter() {
    Host h;
    hc::Adapter a;
    a.set_host(&h.api);
    a.initialize();
    a.peer(true);
    a.game_state(GAMESTATE_INGAME);
    a.pulse();
    h.now += 10000;
    a.pulse();
    check(h.sent.empty(), "missing safe observation probed");
    check(a.command("open"), "manual open");
    auto b = response(id(h));
    a.packet(hc::kOpcode, b.data(), b.size());
    check(a.fresh(), "list not ready");
    const auto revision = a.revision(), grant = a.state().grant, snapshot = a.state().snapshot;
    check(!a.choose(revision, 10, grant, snapshot), "cooldown missing");
    h.now += 1100;
    check(!a.choose(revision + 1, 10, grant, snapshot) && !a.choose(revision, 99, grant, snapshot) &&
              !a.choose(revision, 10, grant + 1, snapshot),
          "stale tuple sent");
    check(a.choose(revision, 10, grant, snapshot) && a.pending(), "confirmed choice not sent");
    b = response(id(h), 3, 0, 0);
    put(b, 44, 99);
    a.packet(hc::kOpcode, b.data(), b.size());
    check(a.pending(), "wrong home acknowledged");
    put(b, 44, 20);
    a.packet(hc::kOpcode, b.data(), b.size());
    check(!a.pending() && !a.fresh() && a.state().current_home == 20, "correct home not confirmed");
    h.now += 1100;
    check(a.refresh(), "new list");
    b = response(id(h));
    a.packet(hc::kOpcode, b.data(), b.size());
    h.now += 1100;
    check(a.suppress(a.revision(), a.state().grant, a.state().snapshot), "suppress not sent");
    b = response(id(h), 4, 0, 0);
    a.packet(hc::kOpcode, b.data(), b.size());
    check(a.pending(), "suppression without flag accepted");
    put(b, 52, 1);
    a.packet(hc::kOpcode, b.data(), b.size());
    check(!a.pending(), "suppression flag rejected");
    h.now += 1100;
    check(a.refresh(), "timeout fixture request");
    const auto old = id(h);
    h.now += 10000;
    a.pulse();
    check(!a.pending(), "request did not time out");
    b = response(old);
    a.packet(hc::kOpcode, b.data(), b.size());
    check(!a.fresh(), "late reply admitted");
    check(a.refresh(), "identity fixture request");
    b = response(id(h));
    ++h.incarnation;
    a.packet(hc::kOpcode, b.data(), b.size());
    check(!a.pending() && a.state().entries.empty(), "identity replacement retained grant");
    a.shutdown();
    check(!h.functions.contains("home_city"), "UI leaked");
}
void automatic_probe() {
    ReceivedHost h;
    hc::Adapter a;
    a.set_host(&h.api);
    a.initialize();
    a.peer(true);
    a.game_state(GAMESTATE_INGAME);
    h.prompt_safe = true;
    a.pulse();
    h.now += 4999;
    a.pulse();
    check(h.sent.empty(), "probe before quiet delay");
    ++h.now;
    a.pulse();
    check(h.sent.size() == 1 && h.sent.back().bytes[6] == std::byte{1}, "safe probe missing");
    h.visibility("home_city", true, false);
    auto b = response(id(h), 1);
    a.packet(hc::kOpcode, b.data(), b.size());
    check(a.shown(), "new grant not prompted");
    h.visibility("home_city", true, true);
    a.begin_zone();
    a.game_state(GAMESTATE_INGAME);
    a.pulse();
    h.now += 5000;
    a.pulse();
    b = response(id(h), 1);
    a.packet(hc::kOpcode, b.data(), b.size());
    check(!a.shown(), "same grant reprompted after zone");
    h.now += 30000;
    a.pulse();
    check(a.pending(), "read-only polling missing");
    b = response(id(h), 1);
    put(b, 16, 99, 8);
    put(b, 52, 1);
    a.packet(hc::kOpcode, b.data(), b.size());
    check(!a.shown(), "suppressed grant prompted");
    a.game_state(GAMESTATE_CHARSELECT);
    a.game_state(GAMESTATE_INGAME);
    a.pulse();
    h.now += 5000;
    a.pulse();
    b = response(id(h), 1);
    a.packet(hc::kOpcode, b.data(), b.size());
    check(a.shown(), "login reminder not restored");
}
void numeric_command_preselection() {
    Host h; hc::Adapter a;
    a.set_host(&h.api); a.peer(true); a.game_state(GAMESTATE_INGAME);
    check(a.command(" 10 ") && a.shown() && a.pending(), "numeric command did not request choices");
    check(h.sent.size() == 1 && h.sent.back().bytes[6] == std::byte{2} && !a.preselection(),
          "numeric command mutated home or selected an unreceived ID");
    const auto count = h.sent.size();
    for (const auto *invalid : {"0", "-1", "+10", "10 extra", "4294967296", "10\n20"})
        check(!a.command(invalid) && h.sent.size() == count && a.message().starts_with("Usage:"), "invalid city ID silently accepted");
    auto bytes = response(id(h)); a.packet(hc::kOpcode, bytes.data(), bytes.size());
    check(a.preselection() == 10 && a.fresh() && h.sent.size() == count, "returned numeric choice did not preselect without committing");
    check(a.command("99") && !a.preselection() && a.message().find("not available") != std::string::npos,
          "unavailable requested ID remained selected");
    check(a.command("10") && a.preselection() == 10 && h.sent.size() == count, "fresh authorized choice not preselected");
    h.now += 1100; check(a.refresh(), "refresh for queued selection failed");
    check(a.command("10") && !a.preselection(), "inflight selection used old list");
    bytes = response(id(h)); a.packet(hc::kOpcode, bytes.data(), bytes.size());
    check(a.preselection() == 10, "requested ID lost across matching received snapshot");
    h.now += 1100; check(a.refresh(), "identity selection request failed");
    check(a.command("10"), "queued identity selection rejected");
    bytes = response(id(h)); ++h.incarnation; a.packet(hc::kOpcode, bytes.data(), bytes.size());
    check(!a.preselection() && a.state().entries.empty() && !a.shown(), "requested city crossed character incarnation");
    a.begin_zone(); a.game_state(GAMESTATE_INGAME); check(a.command("open") && !a.preselection(), "new zone reused requested city");
}
} // namespace
int main() {
    try {
        codec();
        adapter();
        automatic_probe();
        numeric_command_preselection();
        received_visibility<hc::Adapter>("home_city");
        std::cout << "Home city PASS " << checks << " checks\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
