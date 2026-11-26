// SPDX-License-Identifier: MIT
#include "expedition_journal.h"
#include "citadel_test_host.h"

#include <deque>
#include <iostream>

namespace {
using citadel_test::Bytes;
using citadel_test::check;
using bastion::journal::kOpcode;
constexpr std::uint32_t kMagic = 0x31524a42;
constexpr char kPage[] = "https://bastiongame.com/library/game/expedition-journal";
constexpr char kReply[] = R"json({"type":"expeditionJournalStatus","version":1,"mode":"normal","state":{"name":"\";invalid()"}})json";

struct Rig : citadel_test::Host {
    bastion::journal::Adapter journal;
    std::deque<std::string> messages;
    std::vector<std::string> evaluations;
    std::string address;
    bool loading = false;
    unsigned closes = 0;

    Rig() {
        api.web_open = [](void* host, const char* url, int, int) -> std::uint32_t {
            auto& self = *static_cast<Rig*>(static_cast<Host*>(host));
            self.address = url;
            return 123;
        };
        api.web_close = [](void* host, std::uint32_t) {
            auto& self = *static_cast<Rig*>(static_cast<Host*>(host));
            ++self.closes;
            self.messages.clear();
        };
        api.web_state = [](void* host, std::uint32_t, HailstoneWebState* state) {
            auto& self = *static_cast<Rig*>(static_cast<Host*>(host));
            std::memcpy(state->url, self.address.c_str(), self.address.size() + 1);
            state->loading = self.loading ? 1 : 0;
            return 1;
        };
        api.web_message = [](void* host, std::uint32_t, char* out, std::size_t size) {
            auto& self = *static_cast<Rig*>(static_cast<Host*>(host));
            if (self.messages.empty()) return -1;
            const auto message = std::move(self.messages.front());
            self.messages.pop_front();
            std::memcpy(out, message.data(), std::min(size - 1, message.size()));
            return static_cast<int>(message.size());
        };
        api.web_eval = [](void* host, std::uint32_t, const char* script) {
            auto& self = *static_cast<Rig*>(static_cast<Host*>(host));
            self.evaluations.emplace_back(script);
            return 1;
        };
        journal.set_host(&api);
        journal.initialize();
    }

    void open() {
        journal.peer(true);
        journal.game_state(GAMESTATE_INGAME);
        check(journal.open() && address == kPage, "official page did not open");
    }

    Bytes response(std::string_view content = kReply) {
        hailstone::ByteReader request(sent.back().bytes);
        check(sent.back().opcode == kOpcode && request.le<std::uint32_t>() == kMagic,
              "request opcode or magic changed");
        hailstone::ByteWriter writer;
        writer.le(kMagic);
        writer.le(request.le<std::uint16_t>());
        writer.le(request.le<std::uint16_t>());
        writer.le(request.le<std::uint32_t>());
        writer.le(static_cast<std::uint32_t>(content.size()));
        auto bytes = writer.take();
        for (char c : content) bytes.push_back(std::byte(static_cast<unsigned char>(c)));
        return bytes;
    }

    void receive(const Bytes& bytes) {
        check(journal.packet(kOpcode, bytes.data(), bytes.size()), "plugin did not consume its zone opcode");
    }
};

void bridge() {
    Rig rig;
    check(!rig.journal.open(), "opened before identifying Bastion");
    rig.open();
    rig.messages = {"BJR1:refresh:normal", "BJR1:open-library"};
    rig.journal.pulse();
    check(rig.sent.empty() && rig.commands.empty(), "accepted message before ready");
    rig.loading = true;
    rig.messages = {"BJR1:ready", "BJR1:refresh:normal"};
    rig.journal.pulse();
    check(rig.sent.empty(), "processed messages before page load");
    rig.loading = false;
    rig.journal.pulse();
    check(rig.sent.size() == 1 && rig.sent.back().bytes.size() == 16, "request missing or not fixed width");
    const auto reply = rig.response();
    auto forged = reply;
    citadel_test::put(forged, 8, 0);
    rig.receive(forged);
    forged = reply;
    forged.pop_back();
    rig.receive(forged);
    rig.journal.pulse();
    check(rig.evaluations.empty(), "accepted bad length or request identity");
    rig.receive(reply);
    rig.journal.pulse();
    check(rig.evaluations.size() == 1 &&
              rig.evaluations.back().find("location.href==") != std::string::npos &&
              rig.evaluations.back().find(kPage) != std::string::npos &&
              rig.evaluations.back().find("expeditionJournalStatus") != std::string::npos,
          "valid reply missing from origin-guarded bridge");
    rig.messages = {"BJR1:open-library:https://example.com", "BJR1:open-library", "BJR1:open-library"};
    rig.journal.pulse();
    check(rig.commands == std::vector<std::string>{"/url https://bastiongame.com/library/encounters"},
          "page can choose another URL or open it repeatedly");

    rig.now += 1100;
    rig.messages = {"BJR1:refresh:copium:0", "BJR1:refresh:copium:4294967296",
                    "BJR1:refresh:copium:170009"};
    rig.journal.pulse();
    hailstone::ByteReader request(rig.sent.back().bytes);
    request.skip(6);
    check(rig.sent.size() == 2 && request.le<std::uint16_t>() == 4, "Copium detail did not use its mode");
    request.skip(4);
    check(request.le<std::uint32_t>() == 170009, "Copium detail lost relic identity");
    const auto late = rig.response();
    rig.journal.begin_zone();
    rig.receive(late);
    rig.journal.game_state(GAMESTATE_INGAME);
    ++rig.incarnation;
    rig.journal.pulse();
    check(rig.evaluations.size() == 3 &&
              rig.evaluations[1].find("expeditionJournalUnavailable") != std::string::npos &&
              rig.evaluations[2].find("expeditionJournalAvailable") != std::string::npos,
          "zone transition retained stale state or did not request fresh state");

    rig.messages = {"BJR1:refresh:hard"};
    rig.journal.pulse();
    check(rig.sent.size() == 3, "new zone did not establish an independent request");
    rig.now += 7000;
    rig.journal.pulse();
    check(rig.evaluations.back().find("expeditionJournalUnavailable") != std::string::npos &&
              rig.evaluations.back().find("hard") != std::string::npos,
          "timeout did not identify the attempted mode");
    rig.address = "https://bastiongame.com/library/encounters";
    rig.messages = {"BJR1:ready", "BJR1:refresh:normal"};
    rig.journal.pulse();
    check(rig.closes == 1 && rig.sent.size() == 3, "navigation let a different path access the journal");
}

void bundled_view() {
    Rig rig;
    rig.open();
    rig.messages = {"BJR1:bundled-ready", "BJR1:refresh:normal"};
    rig.journal.pulse();
    check(rig.sent.empty(), "accepted a bundle handshake before sending the bundle");
    rig.now += 1800;
    rig.journal.pulse();
    check(rig.evaluations.size() == 1 &&
              rig.evaluations.front().find("BJR1:bundled-ready") != std::string::npos,
          "missing hosted route did not receive the bundled journal");
    rig.messages = {"BJR1:ready", "BJR1:refresh:normal"};
    rig.journal.pulse();
    check(rig.sent.empty(), "hosted page handshake activated the bundled journal");
    rig.messages = {"BJR1:bundled-ready", "BJR1:refresh:normal"};
    rig.journal.pulse();
    rig.receive(rig.response());
    rig.journal.pulse();
    check(rig.evaluations.size() == 2 &&
              rig.evaluations.back().find("bastionExpeditionJournalBundled===true") != std::string::npos,
          "bundled page received private data without the document guard");

    Rig redirect;
    redirect.open();
    redirect.address = "https://bastiongame.com/";
    redirect.now += 10001;
    redirect.journal.pulse();
    check(redirect.address == "https://bastiongame.com/library/encounters",
          "unavailable primary location did not use the fixed Library fallback");
}
} // namespace

int main() {
    try {
        bridge();
        bundled_view();
        std::cout << citadel_test::checks << " expedition journal checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
