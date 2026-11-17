#include "factions_addon.h"
#include "citadel_test_host.h"
#include "received_tools_test_host.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>
namespace fa = bastion::factions;
using namespace citadel_test;
namespace {
struct Catalog {
    Bytes categories = Bytes(264), entries = Bytes(504);
    std::uint64_t hash = 0;
    Catalog() {
        for (unsigned i = 0; i < 2; ++i) {
            const auto c = 132 * std::size_t(i);
            put(categories, c, i + 1);
            put(categories, c + 4, i ? 1 : 0);
            text(categories, c + 12, i ? "Child" : "Parent");
            const auto e = 252 * std::size_t(i);
            put(entries, e, 100 + i);
            put(entries, e + 4, 50 + i);
            put(entries, e + 8, std::uint32_t(-100));
            put(entries, e + 12, 100);
            put(entries, e + 16, 75);
            put(entries, e + 20, 90);
            put(entries, e + 24, 5, 1);
            put(entries, e + 25, 1, 1);
            put(entries, e + 28, i + 1);
            text(entries, e + 156, i ? "Second" : "First");
        }
        rehash();
    }
    void rehash() {
        hash = UINT64_C(14695981039346656037);
        for (const auto *rows : {&categories, &entries})
            for (const auto b : *rows) {
                hash ^= std::to_integer<std::uint8_t>(b);
                hash *= UINT64_C(1099511628211);
            }
        if (!hash)
            hash = 1;
    }
    Bytes packet(std::uint32_t id, std::uint32_t session, unsigned kind = 2) const {
        Bytes b(48);
        put(b, 0, 0x31434146);
        put(b, 4, 1, 2);
        put(b, 6, kind, 2);
        put(b, 8, id);
        put(b, 12, session);
        put(b, 16, 77);
        put(b, 24, hash, 8);
        put(b, 32, 2);
        put(b, 36, 2);
        if (kind == 2 || kind == 3) {
            put(b, 44, 2);
            const auto &rows = kind == 2 ? categories : entries;
            b.insert(b.end(), rows.begin(), rows.end());
        }
        return b;
    }
};
std::pair<std::uint32_t, std::uint32_t> ids(const Host &h) {
    hailstone::ByteReader r(h.sent.back().bytes);
    r.skip(8);
    const auto id = r.le<std::uint32_t>();
    return {id, r.le<std::uint32_t>()};
}
void codec() {
    Catalog c;
    auto b = c.packet(1, 2);
    const auto cats = fa::decode(b), entries = fa::decode(c.packet(1, 2, 3));
    check(cats && entries && fa::validate(cats->category_rows, entries->entry_rows, c.hash), "literal snapshot/hash");
    check(fa::belongs(entries->entry_rows[1], 1, cats->category_rows) &&
              !fa::belongs(entries->entry_rows[0], 2, cats->category_rows),
          "ancestor membership");
    check(fa::gauge(entries->entry_rows[0]) == 750 && std::string(fa::reaction_name(5)) == "Indifferent",
          "earned normalized gauge");
    for (unsigned kind : {2u, 3u}) {
        b = c.packet(1, 2, kind);
        for (std::size_t n = 0; n < b.size(); ++n)
            check(!fa::decode(std::span(b).first(n)), "truncated chunk");
        auto bad = b;
        bad.push_back(std::byte{});
        check(!fa::decode(bad), "trailing chunk");
    }
    for (std::size_t at : {std::size_t(0), std::size_t(4), std::size_t(6), std::size_t(20), std::size_t(32),
                           std::size_t(36), std::size_t(40), std::size_t(44)}) {
        auto bad = c.packet(1, 2);
        put(bad, at, 0xffffffff);
        check(!fa::decode(bad), "invalid chunk envelope");
    }
    auto cycle = c;
    put(cycle.categories, 4, 2);
    cycle.rehash();
    auto p = fa::decode(cycle.packet(1, 2));
    check(p && !fa::validate(p->category_rows, entries->entry_rows, cycle.hash), "category cycle");
    auto missing = c;
    put(missing.entries, 28, 99);
    missing.rehash();
    auto e = fa::decode(missing.packet(1, 2, 3));
    check(e && !fa::validate(cats->category_rows, e->entry_rows, missing.hash), "unknown category");
    auto wrong = c;
    wrong.entries[200] = std::byte{'x'};
    auto ep = fa::decode(wrong.packet(1, 2, 3));
    check(ep && !fa::validate(cats->category_rows, ep->entry_rows, c.hash), "wire padding checksum ignored");
    const auto request = fa::request(3, 4, c.hash);
    check(request.size() == 28 && request[6] == std::byte{1} && request[8] == std::byte{3} &&
              request[12] == std::byte{4},
          "request offsets");
    check(fa::request(0, 1).empty() && fa::request(1, 0).empty(), "zero request/session emitted");
}
void adapter() {
    Host h;
    fa::Adapter a;
    Catalog c;
    a.set_host(&h.api);
    a.initialize();
    a.peer(true);
    a.game_state(GAMESTATE_INGAME);
    check(a.command("open"), "open failed");
    auto [id, session] = ids(h);
    auto entries = c.packet(id, session, 3);
    a.packet(fa::kOpcode, entries.data(), entries.size());
    check(a.entries().empty() && a.pending(), "partial entries published");
    auto cats = c.packet(id, session);
    a.packet(fa::kOpcode, cats.data(), cats.size());
    check(a.fresh() && a.entries().size() == 2 && a.categories().size() == 2, "atomic snapshot unavailable");
    const auto revision = a.revision();
    h.now += 2499;
    a.pulse();
    check(!a.pending(), "faction refresh fired before 2.5 seconds");
    ++h.now;
    a.pulse();
    check(a.pending() && h.sent.size() == 2, "shown stale catalog did not refresh");
    auto current = ids(h);
    auto unchanged = c.packet(current.first, current.second, 4);
    a.packet(fa::kOpcode, unchanged.data(), unchanged.size());
    check(a.fresh() && a.revision() == revision, "unchanged catalog revised or rejected");
    h.now += 1100;
    check(a.refresh(true), "force refresh");
    current = ids(h);
    cats = c.packet(current.first, current.second);
    a.packet(fa::kOpcode, cats.data(), cats.size());
    check(a.pending(), "category only ended transfer");
    a.packet(fa::kOpcode, cats.data(), cats.size());
    check(!a.pending() && !a.fresh() && a.entries().size() == 2, "duplicate chunk accepted/discarded old rows");
    h.now += 1100;
    check(a.refresh(true), "post-error request");
    current = ids(h);
    cats = c.packet(current.first, current.second);
    a.packet(fa::kOpcode, cats.data(), cats.size());
    entries = c.packet(current.first, current.second, 3);
    put(entries, 24, c.hash + 1, 8);
    a.packet(fa::kOpcode, entries.data(), entries.size());
    check(!a.pending(), "conflicting snapshot chunks");
    h.now += 1100;
    check(a.refresh(), "timeout request");
    const auto old = ids(h);
    h.now += 10000;
    a.pulse();
    check(!a.pending(), "inactivity timeout");
    const auto count = h.sent.size();
    h.now += 2499;
    a.pulse();
    check(h.sent.size() == count, "retry before backoff");
    ++h.now;
    a.pulse();
    check(h.sent.size() == count + 1 && a.pending(), "bounded retry missing");
    cats = c.packet(old.first, old.second);
    a.packet(fa::kOpcode, cats.data(), cats.size());
    check(a.pending(), "old transfer affected retry");
    ++h.incarnation;
    a.pulse();
    check(a.entries().empty() && !a.pending(), "character incarnation leaked catalog");
    a.begin_zone();
    a.game_state(GAMESTATE_INGAME);
    check(a.command("open"), "new zone open");
    current = ids(h);
    check(current.second != session, "session reused across zone");
    a.peer(false);
    check(!a.packet(fa::kOpcode, nullptr, 0), "nonpeer consumed");
    a.shutdown();
    check(!h.functions.contains("factions"), "UI leaked");
}
void commands_and_export() {
    Host h;
    fa::Adapter a;
    Catalog c;
    a.set_host(&h.api);
    a.peer(true);
    a.game_state(GAMESTATE_INGAME);
    check(a.command("Ring Of Scale") && a.search() == "Ring Of Scale" && a.shown(), "partial-name command lost case/search");
    check(!a.command("Ring\n/say bad"), "multiline faction query accepted");
    const auto [id, session] = ids(h);
    auto cats = c.packet(id, session), entries = c.packet(id, session, 3);
    a.packet(fa::kOpcode, cats.data(), cats.size());
    a.packet(fa::kOpcode, entries.data(), entries.size());
    check(a.command("") && !a.shown() && a.command("") && a.shown(), "faction command did not toggle");
    const auto temporary = std::filesystem::temp_directory_path() /
        ("hailstone-factions-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(temporary), "scratch folder unavailable");
    const auto directory = temporary.string();
    struct ExportHost : Host { std::string directory; } exporter;
    // Keep the adapter's identity while swapping in the SDK file service.
    exporter.api = h.api;
    exporter.directory = directory;
    exporter.api.host = &exporter;
    exporter.api.config_dir = [](void *self, char *out, std::size_t size) {
        const auto &name = static_cast<ExportHost *>(self)->directory;
        if (name.size() + 1 > size) return 0;
        std::memcpy(out, name.c_str(), name.size() + 1); return 1;
    };
    // Rebuild a complete session on the file-capable host.
    a.set_host(&exporter.api); a.peer(true); a.game_state(GAMESTATE_INGAME); a.command("open");
    const auto current = ids(exporter);
    cats = c.packet(current.first, current.second); entries = c.packet(current.first, current.second, 3);
    a.packet(fa::kOpcode, cats.data(), cats.size()); a.packet(fa::kOpcode, entries.data(), entries.size());
    check(a.export_file("\"My Factions\""), "fresh quoted export failed");
    const auto output = temporary / "exports" / "My Factions.txt";
    std::ifstream file(output, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(file)), {}); file.close();
    check(content == "ID\tName\tStandingValue\tPointsToMax\n100\tFirst\t50\t50\n101\tSecond\t51\t49\n", "export omitted/changed snapshot rows");
    for (const auto *name : {"../outside", "folder/file", "C:\\escape", "CON.txt", "nul", "file.", "file "})
        check(!a.export_file(name), "unsafe export path accepted");
    exporter.now += 15000;
    check(!a.export_file("stale"), "stale snapshot exported");
    check(std::filesystem::remove(output), "scratch export cleanup");
    check(std::filesystem::remove(temporary / "exports") && std::filesystem::remove(temporary), "scratch folder cleanup");
}
} // namespace
int main() {
    try {
        codec();
        adapter();
        commands_and_export();
        received_visibility<fa::Adapter>("factions");
        std::cout << "Factions PASS " << checks << " checks\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
