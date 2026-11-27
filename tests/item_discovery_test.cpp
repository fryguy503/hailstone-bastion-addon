#include "item_discovery.h"

#include "hailstone_byte_io.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint16_t kReadBook = 0x72df;
constexpr std::size_t kPacketSize = 8216, kTextOffset = 22;
using Bytes = std::vector<std::byte>;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Rig {
    bastion::ItemDiscovery discovery;
    PluginHostApi api{};
    std::uint64_t now = 100;
    bool in_zone = true, send_success = true;
    std::vector<Bytes> sent;
    std::function<void()> after_send;

    Rig() {
        api.size = sizeof(api);
        api.host = this;
        api.monotonic_ms = [](void* user) { return static_cast<Rig*>(user)->now; };
        api.get_player = [](void* user, HailstonePlayer* out) {
            *out = {};
            out->size = sizeof(*out);
            out->in_zone = static_cast<Rig*>(user)->in_zone;
            return 1;
        };
        api.send_packet = [](void* user, int stream, std::uint16_t opcode, const void* data, std::size_t size) {
            auto& r = *static_cast<Rig*>(user);
            check(stream == HAILSTONE_STREAM_ZONE && opcode == kReadBook && size == kPacketSize,
                "Incorrect ReadBook request envelope");
            const auto* begin = static_cast<const std::byte*>(data);
            r.sent.emplace_back(begin, begin + size);
            if (auto callback = std::exchange(r.after_send, {})) callback();
            return r.send_success ? 1 : 0;
        };
        discovery.set_host(&api);
    }

    void start() { discovery.peer(true); discovery.game_state(GAMESTATE_INGAME); }

    std::string_view describe(std::uint32_t id) {
        HailstoneItem item{};
        item.size = sizeof(item);
        item.id = id;
        const auto* description = discovery.item_description_append(&item);
        return description ? description : "";
    }

    bool receive(const Bytes& bytes, std::uint16_t opcode = kReadBook) {
        return discovery.packet(opcode, bytes.data(), bytes.size());
    }

    void advance(std::uint64_t ms) { now += ms; discovery.pulse(); }

    Bytes reply(std::size_t index, std::string_view text) const {
        auto reply = sent.at(index);
        check(text.size() < reply.size() - kTextOffset, "Fixture text is too long");
        std::fill(reply.begin() + kTextOffset, reply.end(), std::byte{0});
        std::memcpy(reply.data() + kTextOffset, text.data(), text.size());
        return reply;
    }
};

std::uint32_t check_request(const Bytes& message, std::string_view tagged_id) {
    hailstone::ByteReader reader(message);
    check(reader.le<std::uint32_t>() == 0xfe, "Unexpected item-info window");
    check(reader.le<std::int16_t>() == -1 && reader.le<std::int16_t>() == -1 &&
        reader.le<std::int16_t>() == -1 && reader.le<std::int16_t>() == 0,
        "Item-info lookup specified a possessions slot");
    check(reader.le<std::uint32_t>() == 2, "Wrong ReadBook type");
    const auto target = reader.le<std::uint32_t>();
    check((target & 0xc0000000) == 0x40000000, "Missing request marker");
    check(reader.le<std::uint8_t>() == 0 && reader.le<std::uint8_t>() == 0 && reader.cstring() == tagged_id,
        "Wrong flags or discovery tag");
    while (!reader.empty()) check(reader.le<std::uint8_t>() == 0, "ReadBook request is not padded with NULs");
    return target;
}

void overlapping_items_and_unrelated_books() {
    Rig r;
    r.discovery.game_state(GAMESTATE_INGAME);
    check(r.describe(71).empty(), "Unselected peer returned discovery text");
    r.discovery.pulse();
    check(r.sent.empty(), "Unselected peer received a lookup");
    r.start();
    check(r.describe(0).empty() && r.describe(71).empty() && r.describe(71).empty() && r.describe(90).empty(),
        "An unknown item has discovery text");
    r.discovery.pulse();
    check(r.sent.size() == 1, "First lookup did not send");
    const auto first = check_request(r.sent[0], "71#");
    r.advance(99);
    check(r.sent.size() == 1, "Lookup exceeded the send rate");
    r.advance(1);
    check(r.sent.size() == 2, "First pending request blocked a second item");
    check(first != check_request(r.sent[1], "90#"), "Two requests reused a target marker");

    auto stock = r.reply(0, "Ordinary book text");
    stock[12] = std::byte{1};
    check(!r.receive(stock), "Stock book reply was consumed");
    stock[12] = std::byte{0};
    check(!r.receive(stock), "Stock note reply was consumed");
    stock[12] = std::byte{2};
    stock[0] = std::byte{1};
    check(!r.receive(stock), "Unrelated item-info reply was consumed");
    const auto unrelated_opcode_reply = r.reply(0, "Discovered By: Another<br>");
    check(!r.receive(unrelated_opcode_reply, 1), "Unrelated opcode was consumed");
    check(!r.discovery.packet(kReadBook, nullptr, 0), "Null packet was consumed");

    const auto rev = r.discovery.revision();
    const auto discovered = r.reply(1, "<c>Charm description</c><br>Discovered By: Lyric<br>");
    check(r.receive(discovered) && r.describe(90) == "Discovered By: Lyric" && r.describe(71).empty(),
        "Reply attached to the wrong inspected item or included charm markup");
    check(r.discovery.revision() == rev + 1, "Open item windows were not invalidated");
    check(r.receive(discovered) && r.discovery.revision() == rev + 1, "Duplicate reply invalidated item windows");
    r.advance(2900);
    check(r.describe(71).empty() && r.describe(90) == "Discovered By: Lyric", "Timeout disturbed another item's data");
    r.advance(300);
    check(r.sent.size() == 2, "Missing reply was retried while rendering");
    check(r.receive(r.reply(0, "Discovered By: Delayed<br>")) && r.describe(71).empty(),
        "Timed out reply reached the item window");
    r.advance(29700);
    r.describe(71);
    r.advance(100);
    check(r.sent.size() == 3 && check_request(r.sent[2], "71#") != first, "Retry reused its old target marker");
    check(r.receive(r.reply(2, "Discovered By: Finder<br>")) && r.describe(71) == "Discovered By: Finder",
        "A later item inspection could not recover after timeout");
}

void invalid_responses_and_zone_changes() {
    Rig r;
    r.start();
    r.in_zone = false;
    r.describe(123);
    r.discovery.pulse();
    check(r.sent.empty(), "Sent to the zone before the player arrived");
    r.in_zone = true;
    r.advance(100);
    const auto first = check_request(r.sent.at(0), "123#");
    const auto entry = r.reply(0, "Discovered By: Finder<br>");
    auto invalid = entry;
    invalid[4] = std::byte{23};
    check(r.receive(invalid) && r.describe(123).empty(), "Reply for a book slot changed the description");
    invalid = entry;
    invalid[20] = std::byte{1};
    check(r.receive(invalid) && r.describe(123).empty(), "Castable reply changed the description");
    invalid = entry;
    std::fill(invalid.begin() + kTextOffset, invalid.end(), std::byte{'A'});
    check(r.receive(invalid) && r.describe(123).empty(), "Unterminated text changed the description");
    check(r.receive(r.reply(0, "Discovered By: <script>attacker</script><br>")) && r.describe(123).empty(),
        "Markup passed through to the description");

    r.discovery.begin_zone();
    check(r.receive(entry) && r.describe(123).empty(), "A delayed response after begin-zone altered discovery");
    r.discovery.game_state(GAMESTATE_INGAME);
    r.describe(123);
    r.discovery.pulse();
    check(check_request(r.sent.at(1), "123#") != first, "Reopened item reused its previous zone token");
    check(r.receive(entry) && r.describe(123).empty(), "Old zone reply passed for a new pending lookup");
    check(r.receive(r.reply(1, "Discovered By: Other<br>")) && r.describe(123) == "Discovered By: Other",
        "Current zone reply did not update discovery");
    const auto rev = r.discovery.revision();
    r.discovery.game_state(GAMESTATE_CHARSELECT);
    check(r.describe(123).empty() && r.discovery.revision() != rev, "Character select retained prior discoveries");
}

void failure_and_capacity() {
    Rig r;
    r.start();
    r.describe(1);
    r.describe(2);
    r.send_success = false;
    r.after_send = [&] { r.discovery.begin_zone(); };
    r.discovery.pulse();
    r.discovery.game_state(GAMESTATE_INGAME);
    r.advance(500);
    check(r.sent.size() == 1, "A failed send restored discarded zone requests");
    r.send_success = true;
    for (std::uint32_t id = 1000; id < 1500; ++id) r.describe(id);
    for (int i = 0; i < 500; ++i) r.advance(100);
    check(r.sent.size() == 257, "Lookup queue grew without bound");
    r.describe(1500);
    r.advance(100);
    check(r.sent.size() == 258, "A full cache could not admit a new lookup after timeouts");
}

} // namespace

int main() {
    try {
        overlapping_items_and_unrelated_books();
        invalid_responses_and_zone_changes();
        failure_and_capacity();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
