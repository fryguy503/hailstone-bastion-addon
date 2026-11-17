#include "gear_score_protocol.h"
#include "gear_score_state.h"
#include "hailstone_byte_io.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace gs = bastion::gear_score;
namespace {
std::size_t checks = 0;
void check(bool condition, const char* why) { ++checks; if (!condition) throw std::runtime_error(why); }
using Bytes = std::array<std::byte, 68>;
void put(Bytes& bytes, std::size_t offset, std::uint32_t value, unsigned width = 4) {
    for (unsigned i = 0; i < width; ++i) bytes[offset + i] = std::byte(static_cast<std::uint8_t>(value >> (8 * i)));
}
Bytes literal(unsigned action = 1, std::uint32_t session = 23) {
    Bytes bytes{}; bytes[0] = std::byte{0x49}; bytes[1] = std::byte{0x4c}; bytes[2] = std::byte{0x56}; bytes[3] = std::byte{0x31};
    bytes[4] = std::byte{1}; bytes[5] = std::byte(static_cast<std::uint8_t>(action)); put(bytes, 8, session); return bytes;
}
gs::Packet ack(std::uint32_t session = 23, std::uint32_t generation = 11) {
    gs::Packet p; p.action = gs::Action::HelloAck; p.session = session; p.generation = generation; return p;
}
gs::Packet item(std::uint32_t id = 900, std::uint32_t revision = 2) {
    auto p = ack(); p.action = gs::Action::Item; p.revision = revision; p.key.item = id;
    p.key.augments = {7, 0, 8, 0, 0, 0}; p.flags = gs::BaseValid | gs::ConfigurationValid;
    p.base = 123450; p.configuration = 155499; return p;
}
void exact_wire() {
    auto bytes = literal();
    check(gs::hello(0).empty(), "Zero nonce emitted a Hello");
    const auto hello = gs::hello(23);
    check(hello.size() == bytes.size() && std::equal(hello.begin(), hello.end(), bytes.begin()), "Hello changed any literal reserved field");
    check(gs::decode(bytes).has_value(), "Literal Hello was rejected");
    for (std::size_t offset : {std::size_t(0), std::size_t(4), std::size_t(5), std::size_t(6), std::size_t(12), std::size_t(16), std::size_t(20), std::size_t(48), std::size_t(52)}) {
        auto bad = bytes; bad[offset] = std::byte{0xff}; check(!gs::decode(bad), "Malformed Hello envelope was accepted");
    }
    check(!gs::decode(std::span(bytes).first(67)) && !gs::decode({}), "Truncated wire packet was accepted");
    auto long_bytes = std::vector<std::byte>(bytes.begin(), bytes.end()); long_bytes.push_back(std::byte{});
    check(!gs::decode(long_bytes), "Trailing wire bytes were ignored");
    bytes = literal(4); put(bytes, 6, 3, 2); put(bytes, 12, 11); put(bytes, 16, 0xffffffff);
    put(bytes, 20, 100000); put(bytes, 24, 65534); put(bytes, 28, 65535); put(bytes, 44, 0xffffffff);
    put(bytes, 52, 1000000000); put(bytes, 56, 1000000000);
    const auto p = gs::decode(bytes);
    check(p && p->key.item == 100000 && p->key.augments[0] == 65534 && p->key.augments[1] == 65535 &&
        p->key.augments[5] == 0xffffffff && p->revision == 0xffffffff && p->configuration == gs::kMaximumMilli,
        "Full-width key, revision or inclusive rating boundary was truncated");
    hailstone::ByteReader independent(hello); independent.skip(8);
    check(independent.le<std::uint32_t>() == 23 && independent.remaining() == 56, "Hello nonce offset/size changed");
    for (std::size_t offset : {std::size_t(52), std::size_t(56), std::size_t(60), std::size_t(64)}) {
        auto bad = bytes; put(bad, offset, 1000000001); check(!gs::decode(bad), "Excessive rating was accepted");
    }
    auto bad = bytes; put(bad, 48, 1); check(!gs::decode(bad), "Unsupported reference state was accepted");
    bad = bytes; put(bad, 6, 128, 2); check(!gs::decode(bad), "Unknown flag was accepted");
    bad = bytes; put(bad, 8, 0); check(!gs::decode(bad), "Zero session was accepted");
    bad = bytes; put(bad, 6, gs::ConfigurationValid, 2); check(!gs::decode(bad), "Configuration without BaseValid was accepted");
    bad = bytes; put(bad, 6, gs::BaseValid | gs::NotGear, 2); check(!gs::decode(bad), "NotGear with nonzero values was accepted");
    bad = bytes; put(bad, 6, gs::EquippedValid, 2); check(!gs::decode(bad), "Item accepted summary flags");
    bad = literal(5); put(bad, 12, 11); put(bad, 6, gs::OwnedStale, 2);
    check(!gs::decode(bad), "Stale Owned without OwnedValid was accepted");
    put(bad, 6, gs::OwnedStale | gs::OwnedValid, 2); check(gs::decode(bad).has_value(), "Valid stale summary rejected");
    put(bad, 20, 9); check(!gs::decode(bad), "Summary accepted an item key");
}
void lifecycle_and_revisions() {
    gs::State s;
    check(s.description(item().key, true).empty(), "Unsupported peer appended item text");
    s.begin(23); const auto started = s.revision();
    check(s.receive(item()) == gs::Receive::Ignored && !s.ready(), "Item negotiated itself before ACK");
    check(s.receive(ack(24)) == gs::Receive::Ignored, "Old session negotiated current cache");
    check(s.receive(ack()) == gs::Receive::Changed && s.ready() && s.revision() != started, "ACK did not negotiate");
    auto p = item();
    check(s.receive(p) == gs::Receive::Changed && s.templates() == 1 && s.configurations() == 1, "Item did not fill both caches");
    const auto updated = s.revision();
    check(s.receive(p) == gs::Receive::Ignored && s.revision() == updated, "Duplicate item refreshed presentation unnecessarily");
    check(s.receive(ack()) == gs::Receive::Ignored && s.configurations() == 1, "Retry ACK erased a ready cache");
    p.revision = 1; p.base = p.configuration = 999000;
    check(s.receive(p) == gs::Receive::Ignored && s.description(p.key, true).find("155") != std::string::npos, "Older item displaced live rating");
    auto reset = ack(); reset.action = gs::Action::Reset; reset.revision = 3;
    check(s.receive(reset) == gs::Receive::Changed && s.templates() == 0 && s.configurations() == 0, "Reset retained catalog entries");
    check(s.receive(item()) == gs::Receive::Ignored, "Pre-reset revision was accepted");
    p = item(900, 3); check(s.receive(p) == gs::Receive::Changed, "Reset floor was incorrectly exclusive");
    check(s.receive(reset) == gs::Receive::Ignored && s.templates() == 1, "Duplicate reset erased new entries");
    reset.generation = 12; reset.revision = 0;
    check(s.receive(reset) == gs::Receive::Changed && s.generation() == 12 && s.templates() == 0, "New catalog reset failed");
    check(s.receive(item()) == gs::Receive::Ignored, "Old generation Item crossed reset");
    s.begin(24); check(!s.ready() && s.session() == 24 && s.equipped() == "Unavailable", "New session inherited negotiation/summary");
    check(s.receive(ack()) == gs::Receive::Ignored, "Old-zone ACK negotiated new zone");
    s.retire(); check(!s.session() && s.description(item().key, true).empty(), "Retired peer exposed text");
}
void presentation_and_summary() {
    gs::State s; s.begin(23); s.receive(ack());
    auto p = item(); s.receive(p);
    check(s.description(p.key, true) == "ilvl: 155\nBase ilvl: 123 | Augments: +32", "Combined/plain delta formatting changed");
    check(s.description(p.key, false) == "Base ilvl: 123", "Incomplete configuration used a combined rating");
    auto other = p.key; other.augments[1] = 9;
    check(s.description(other, true) == "Base ilvl: 123", "Unknown installed configuration invented an augmented rating");
    check(s.description(gs::Key{123}, true) == "ilvl: Unavailable", "Negotiated missing metadata invented zero");
    p.key.item = 901; p.flags = gs::BaseValid | gs::ConfigurationValid | gs::NotGear; p.base = p.configuration = 0; s.receive(p);
    check(s.description(p.key, true) == "ilvl: Not gear", "Known non-gear was shown as missing/zero");
    p.key.item = 902; p.flags = gs::BaseValid | gs::ConfigurationValid; s.receive(p);
    check(s.description(p.key, true) == "ilvl: 0", "Known zero rating became unavailable");
    p.key.item = 903; p.flags |= gs::ReviewRequired; p.base = 2500; p.configuration = 1499; s.receive(p);
    check(s.description(p.key, true) == "ilvl: 1 (provisional)\nBase ilvl: 3 | Augments: -2", "Provisional/negative rounded delta changed");
    check(gs::format_level(499) == "0" && gs::format_level(500) == "1" && gs::format_level(gs::kMaximumMilli) == "1000000", "Integer level rounding boundary changed");
    check(gs::format_average(1249) == "1.2" && gs::format_average(1250) == "1.3", "Average tenth rounding changed");
    auto summary = ack(); summary.action = gs::Action::Summary; summary.revision = 1;
    summary.flags = gs::EquippedValid | gs::OwnedValid | gs::OwnedStale | gs::ReviewRequired; summary.equipped = 1250; summary.owned = 1549;
    check(s.receive(summary) == gs::Receive::Changed && s.equipped() == "1.3 (provisional)" && s.owned() == "1.5 (stale) (provisional)", "Server averages or flags were recomputed/lost");
    summary.equipped = summary.owned = 0;
    check(s.receive(summary) == gs::Receive::Ignored && s.equipped() == "1.3 (provisional)", "Equal summary revision displaced live averages");
    ++summary.revision; summary.flags = 0; s.receive(summary);
    check(s.equipped() == "Unavailable" && s.owned() == "Unavailable", "Summary missing-valid flags invented zero");
    ++summary.revision; summary.flags = gs::EquippedValid | gs::OwnedValid; s.receive(summary);
    check(s.equipped() == "0.0" && s.owned() == "0.0", "Known zero averages became unavailable");
}
void capacity() {
    gs::State s; s.begin(23); s.receive(ack());
    auto p = item(1, 1); p.key.augments = {};
    for (std::uint32_t id = 1; id <= gs::kMaximumTemplates + 1; ++id) { p.key.item = id; check(s.receive(p) == gs::Receive::Changed, "Bounded template insert failed"); }
    check(s.templates() == gs::kMaximumTemplates && s.configurations() == gs::kMaximumConfigurations, "Catalog bound exceeded");
    check(s.description(gs::Key{1}, false) == "ilvl: Unavailable" && s.description(p.key, false) == "Base ilvl: 123", "Bounded template eviction damaged latest key");
    s.begin(23); s.receive(ack()); p = item(99, 1);
    for (std::uint32_t augment = 1; augment <= gs::kMaximumConfigurations + 1; ++augment) {
        p.key.augments = {augment, 0, 0, 0, 0, 0}; check(s.receive(p) == gs::Receive::Changed, "Bounded configuration insert failed");
    }
    check(s.templates() == 1 && s.configurations() == gs::kMaximumConfigurations, "Configuration-only bound failed");
    auto evicted = p.key; evicted.augments[0] = 1;
    check(s.description(evicted, true) == "Base ilvl: 123" && s.description(p.key, true).starts_with("ilvl: 155"), "Evicted configuration did not safely fall back to base");
}
} // namespace
int main() {
    try { exact_wire(); lifecycle_and_revisions(); presentation_and_summary(); capacity();
        std::cout << "ILV1 wire/cache: " << checks << " checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
