#include "gear_score_addon.h"
#include "kinbound.h"
#include "hailstone_byte_io.h"

#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct HailstoneArgs { std::string operation; };
struct HailstoneOut {
    std::map<std::string, std::string> strings;
    std::map<std::string, std::int64_t> numbers;
    std::string key;
    unsigned depth = 0;
};
namespace gs = bastion::gear_score;
namespace {
std::size_t checks = 0;
void check(bool condition, const char* why) { ++checks; if (!condition) throw std::runtime_error(why); }
using Bytes = std::vector<std::byte>;
void put(Bytes& bytes, std::size_t offset, std::uint32_t value, unsigned width = 4) {
    for (unsigned i = 0; i < width; ++i) bytes[offset + i] = std::byte(static_cast<std::uint8_t>(value >> (8 * i)));
}
Bytes response(unsigned action, std::uint32_t session, std::uint32_t revision = 0, std::uint32_t generation = 11) {
    Bytes bytes(68); put(bytes, 0, 0x31564c49); bytes[4] = std::byte{1}; bytes[5] = std::byte(static_cast<std::uint8_t>(action));
    put(bytes, 8, session); put(bytes, 12, generation); put(bytes, 16, revision); return bytes;
}
Bytes rating(std::uint32_t session, unsigned revision = 1) {
    auto bytes = response(4, session, revision); put(bytes, 6, 3, 2); put(bytes, 20, 900); put(bytes, 24, 71);
    put(bytes, 44, 0xffffffff); put(bytes, 52, 124500); put(bytes, 56, 139500); return bytes;
}
std::string copied(const char* text) { return text ? text : ""; }
struct Rig {
    gs::Addon addon;
    PluginHostApi api{};
    HailstonePlayer player{};
    bool readable = true, send_success = true;
    std::uint64_t now = 100;
    std::vector<Bytes> sent;
    std::vector<std::string> logged;
    std::function<void()> after_send;
    std::map<std::string, std::pair<HailstoneUiFn, void*>> functions;
    Rig() {
        api.size = sizeof(api); api.abi = HAILSTONE_PLUGIN_ABI; api.host = this;
        player.size = sizeof(player); player.spawn_id = 1; player.zone_id = 2; player.in_zone = 1;
        player.game_state = GAMESTATE_INGAME; std::memcpy(player.name, "Fixture", 8);
        api.get_player = [](void* user, HailstonePlayer* out) {
            auto& r = *static_cast<Rig*>(user); if (!r.readable) return 0; *out = r.player; return 1;
        };
        api.monotonic_ms = [](void* user) { return static_cast<Rig*>(user)->now; };
        api.send_packet = [](void* user, int stream, std::uint16_t opcode, const void* bytes, std::size_t size) {
            auto& r = *static_cast<Rig*>(user);
            check(stream == HAILSTONE_STREAM_ZONE && opcode == gs::kOpcode && size == 68, "Adapter emitted a non-Hello route");
            const auto* begin = static_cast<const std::byte*>(bytes); r.sent.emplace_back(begin, begin + size);
            const auto callback = std::exchange(r.after_send, {}); if (callback) callback();
            return r.send_success ? 1 : 0;
        };
        api.add_ui_function = [](void* user, const char* name, HailstoneUiFn fn, void* context) {
            static_cast<Rig*>(user)->functions.insert_or_assign(name, std::pair{fn, context}); return 1;
        };
        api.remove_ui_function = [](void* user, const char* name) { static_cast<Rig*>(user)->functions.erase(name); };
        api.arg = [](void*, const HailstoneArgs* args, std::size_t at, HailstoneValue* out) {
            if (!args || at) return 0;
            *out = {}; out->kind = HAILSTONE_STRING; out->s = args->operation.c_str(); return 1;
        };
        api.out_begin_table = [](void*, HailstoneOut* out) { ++out->depth; };
        api.out_end = [](void*, HailstoneOut* out) { check(out->depth != 0, "UI result stack underflow"); --out->depth; };
        api.out_key = [](void*, HailstoneOut* out, const char* key) { out->key = key; };
        api.out_value = [](void*, HailstoneOut* out, const HailstoneValue* value) {
            if (value->kind == HAILSTONE_STRING) out->strings[out->key] = value->s ? value->s : "";
            else if (value->kind == HAILSTONE_INT) out->numbers[out->key] = value->i;
        };
        addon.set_host(&api); addon.initialize();
    }
    ~Rig() { addon.set_host(nullptr); }
    void start() { addon.peer(true); addon.game_state(GAMESTATE_INGAME); addon.pulse(); }
    std::uint32_t nonce() const {
        check(!sent.empty(), "No Hello to read"); hailstone::ByteReader reader(sent.back()); reader.skip(8); return reader.le<std::uint32_t>();
    }
    void deliver(const Bytes& bytes) { check(addon.packet(gs::kOpcode, bytes.data(), bytes.size()), "Selected private response reached stock"); }
    void negotiate() { start(); deliver(response(2, nonce())); }
    HailstoneOut ui(const char* operation) {
        HailstoneArgs args{operation}; HailstoneOut out; const auto fn = functions.at("gear_score");
        check(fn.first(fn.second, &args, &out) == 1 && out.depth == 0, "Read-only UI state failed"); return out;
    }
};
void negotiation() {
    Rig r;
    r.addon.game_state(GAMESTATE_INGAME); r.addon.pulse(); check(r.sent.empty(), "Unselected server received Hello");
    r.send_success = false; r.start(); const auto session = r.nonce();
    check(session != 0 && r.addon.attempts() == 0, "Failed send consumed an attempt or used zero nonce");
    r.now += 1999; r.addon.pulse(); check(r.sent.size() == 1, "Failed send was retried every frame");
    r.now += 1; r.send_success = true; r.addon.pulse();
    check(r.nonce() == session && r.addon.attempts() == 1, "Retry changed nonce or attempt count");
    for (unsigned i = 1; i < 5; ++i) { r.now += 2000; r.addon.pulse(); check(r.nonce() == session, "Bounded retries changed session"); }
    r.now += 200000; r.addon.pulse(); check(r.sent.size() == 6 && r.addon.attempts() == 5, "Successful Hello attempts were not bounded at five");
    check(r.ui("state").numbers.at("can_retry") == 1, "Read-only panel omitted manual retry");
    check(r.ui("retry").numbers.at("retried") == 1, "Explicit negotiation retry failed");
    r.addon.pulse(); check(r.sent.size() == 7 && r.nonce() == session, "Manual retry changed the current nonce");
    r.deliver(response(2, session + 1)); check(!r.addon.state().ready(), "Wrong-session ACK negotiated");
    r.deliver(response(2, session)); check(r.addon.state().ready(), "Matching ACK did not negotiate");
    const auto sends = r.sent.size(); r.now += 10000; r.addon.pulse();
    check(r.sent.size() == sends && !r.addon.retry(), "Ready session continued Hello or reset via retry");
    Bytes invalid(1); const auto revision = r.addon.revision(); r.deliver(invalid);
    check(r.addon.revision() == revision && r.addon.packet(gs::kOpcode, nullptr, 68), "Malformed private packet mutated/escaped adapter");
    check(!r.addon.packet(1, nullptr, 0), "Ordinary opcode consumed");
}
void handoffs_and_host_capabilities() {
    Rig r; r.negotiate(); const auto first = r.nonce(); r.deliver(rating(first));
    r.addon.begin_zone(); check(!r.addon.state().ready() && !r.addon.state().session(), "Zone begin retained ready data");
    r.deliver(response(2, first)); check(!r.addon.state().ready(), "Zoning accepted previous ACK");
    r.addon.game_state(GAMESTATE_INGAME); r.player.in_zone = 0; r.addon.pulse(); check(!r.addon.state().session(), "Game-state alone negotiated before zone readiness");
    r.player.in_zone = 1; r.addon.pulse(); const auto second = r.nonce(); check(second != first, "New zone reused nonce");
    r.deliver(response(2, first)); check(!r.addon.state().ready(), "Old-zone delayed ACK negotiated new zone");
    r.deliver(response(2, second)); r.player.zone_id = 99; r.addon.pulse(); const auto third = r.nonce();
    check(third != second && !r.addon.state().ready(), "Changed player zone inherited catalog without callback");
    r.player.name[0] = 'N'; r.addon.pulse(); check(r.nonce() != third, "Character handoff inherited session");
    r.readable = false; r.addon.pulse(); check(!r.addon.state().session(), "Lost player observation retained cache");
    r.readable = true; r.addon.pulse(); const auto before_peer = r.nonce(); r.addon.peer(false); r.addon.pulse();
    check(!r.addon.packet(gs::kOpcode, nullptr, 68) && !r.addon.label(1801), "Unsupported peer altered stock presentation");
    r.addon.peer(true); r.addon.pulse(); check(r.nonce() != before_peer, "Peer handoff reused nonce");
    r.addon.game_state(GAMESTATE_CHARSELECT); check(!r.addon.state().session() && !r.addon.label(1802), "Character select retained metadata");
    r.addon.set_host(nullptr); check(!r.functions.contains("gear_score"), "Host replacement retained old UI callback");
    r.api.size = offsetof(PluginHostApi, monotonic_ms); r.addon.set_host(&r.api); r.addon.initialize(); r.start();
    check(!r.addon.state().session() && !r.addon.retry() && r.ui("state").numbers.at("can_retry") == 0,
        "Short host clock table fabricated handshake timing/retry availability");
    r.api.size = sizeof(r.api); r.player.size = offsetof(HailstonePlayer, in_zone); r.addon.set_host(&r.api); r.start();
    check(!r.addon.state().session(), "Short player snapshot fabricated zone readiness");
    r.player.size = sizeof(r.player); r.addon.set_host(&r.api); r.addon.peer(true); r.addon.game_state(GAMESTATE_INGAME);
    r.after_send = [&] { r.deliver(response(2, r.nonce())); };
    r.addon.pulse(); check(r.addon.state().ready(), "Synchronous ACK during send was overwritten");
    r.addon.begin_zone(); r.addon.game_state(GAMESTATE_INGAME); r.after_send = [&] { r.addon.peer(false); }; r.send_success = false;
    r.addon.pulse(); check(!r.addon.state().session() && r.addon.attempts() == 0, "Send callback restored retired counters");
}
void item_snapshots_and_summary_ui() {
    Rig r; r.negotiate(); const auto session = r.nonce(); r.deliver(rating(session));
    HailstoneItem item{}; item.size = sizeof(item); item.id = 900; item.configuration_complete = 1;
    item.augment_ids[0] = 71; item.augment_ids[5] = 0xffffffff;
    check(copied(r.addon.item_description_append(&item)) == "ilvl: 140\nBase ilvl: 125 | Augments: +15", "Complete SDK installed key did not select combined rating");
    item.configuration_complete = 0;
    check(copied(r.addon.item_description_append(&item)) == "Base ilvl: 125", "Incomplete SDK snapshot invented configuration");
    item.size = offsetof(HailstoneItem, augment_ids);
    check(copied(r.addon.item_description_append(&item)) == "Base ilvl: 125", "Legacy item ABI failed safe base fallback");
    item.size = sizeof(item); item.configuration_complete = 1; item.augment_ids[1] = 72;
    check(copied(r.addon.item_description_append(&item)) == "Base ilvl: 125", "Different installed configuration reused cached augmented level");
    auto summary = response(5, session, 1); put(summary, 6, 8 | 16 | 32 | 64, 2); put(summary, 60, 12350); put(summary, 64, 14551);
    const auto before = r.addon.revision(); r.deliver(summary);
    check(r.addon.revision() != before && copied(r.addon.label(1801)) == "12.4 (provisional)" &&
        copied(r.addon.label(1802)) == "14.6 (stale) (provisional)", "Server summaries did not reach labels/revision");
    check(!r.addon.label(1800), "Unrelated stock label was rewritten");
    const auto state = r.ui("state");
    check(state.numbers.at("active") == 1 && state.numbers.at("ready") == 1 && state.numbers.at("summary_received") == 1 &&
        state.strings.at("owned") == copied(r.addon.label(1802)), "Summary HUD state lost exact server values");
    auto reset = response(3, session, 0, 12); r.deliver(reset);
    check(copied(r.addon.item_description_append(&item)) == "ilvl: Unavailable" && copied(r.addon.label(1801)) == "Unavailable", "Catalog reset did not remove displayed ratings");
    r.addon.peer(false); check(!r.addon.item_description_append(&item), "Retired peer left an appended line");
}
void independent_presentation() {
    Rig r; r.negotiate(); const auto nonce = r.nonce(); r.deliver(rating(nonce));
    bastion::KinboundCatalog catalog;
    Bytes sharing(12); put(sharing, 0, 0x314e494b); sharing[4] = sharing[5] = std::byte{1}; put(sharing, 8, 900);
    check(catalog.receive(sharing.data(), sharing.size()) == bastion::KinboundCatalog::Receive::Accepted, "sharing catalog failed");
    HailstoneItem item{}; item.size = sizeof(item); item.id = 900; item.configuration_complete = 1; item.augment_ids[0] = 71; item.augment_ids[5] = 0xffffffff;
    const auto combine = [&] { return copied(catalog.item_description_append(&item)) + "\n\n" + copied(r.addon.item_description_append(&item)); };
    const auto combined = combine();
    check(combined.starts_with("KINBOUND\n") && combined.find("\n\nilvl: 140\nBase ilvl: 125 | Augments: +15") != std::string::npos,
        "Description composition replaced sharing text or escaped plaintext twice");
    check(copied(catalog.item_flags(&item, "Magic, Attunable")) == "Magic, Kinbound", "Gear rating rewrote dedicated sharing flags");
    const auto before = r.addon.revision(); r.deliver(response(3, nonce, 0, 12));
    check(r.addon.revision() != before, "Gear reset did not invalidate cached text");
    const auto reset = combine();
    check(reset.starts_with("KINBOUND\n") && reset.ends_with("ilvl: Unavailable"), "Gear reset removed independent sharing presentation");
    r.addon.begin_zone();
    const auto zoned = combine();
    check(zoned.starts_with("KINBOUND\n") && zoned.find("ilvl:") == std::string::npos, "Zone handoff retained rating or removed account metadata");
    check(!r.addon.label(1803), "Addon rewrote unrelated label");
}
void callback_exception_boundary() {
    Rig r;
    // Use the registered C callback, not the adapter's private implementation.
    const auto registered = r.functions.at("gear_score");
    const auto original_arg = r.api.arg;
    r.api.arg = [](void*, const HailstoneArgs*, std::size_t, HailstoneValue*) -> int {
        throw std::runtime_error("injected host argument failure");
    };
    r.api.log = [](void* self, int, const char* message) {
        static_cast<Rig*>(self)->logged.emplace_back(message);
    };
    HailstoneArgs args{"state"}; HailstoneOut out;
    check(registered.first(registered.second, &args, &out) == 0 && out.depth == 0,
          "throwing host callback crossed the registered UI boundary");
    check(r.logged == std::vector<std::string>{"injected host argument failure"}, "callback failure was not logged");
    r.api.arg = [](void*, const HailstoneArgs*, std::size_t, HailstoneValue*) -> int { throw 17; };
    r.api.log = [](void*, int, const char*) { throw std::runtime_error("injected logger failure"); };
    check(registered.first(registered.second, &args, &out) == 0, "nonstandard exception or throwing logger escaped UI boundary");
    r.api.arg = original_arg;
    check(r.ui("state").depth == 0, "registered UI could not recover after a failed callback");
}
} // namespace
int main() {
    try { negotiation(); handoffs_and_host_capabilities(); item_snapshots_and_summary_ui(); independent_presentation(); callback_exception_boundary();
        std::cout << "ILV1 SDK/lifecycle/composition: " << checks << " checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
