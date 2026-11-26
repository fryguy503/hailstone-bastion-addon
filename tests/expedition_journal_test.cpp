// SPDX-License-Identifier: MIT
#include "expedition_journal.h"
#include "citadel_test_host.h"

#include <iostream>

using Json = nlohmann::json;
struct HailstoneArgs {
    std::vector<const char*> values;
};
struct HailstoneOut {
    Json value;
    std::vector<Json*> parents;
    std::string key;

    Json& put(Json item) {
        if (parents.empty()) return value = std::move(item);
        if (parents.back()->is_array()) {
            parents.back()->push_back(std::move(item));
            return parents.back()->back();
        }
        return (*parents.back())[key] = std::move(item);
    }
};

namespace {
using citadel_test::Bytes;
using citadel_test::check;
using bastion::journal::kOpcode;
constexpr std::uint32_t kMagic = 0x31524a42;

struct Rig : citadel_test::Host {
    bastion::journal::Adapter journal;
    std::vector<std::uint32_t> inspected;

    Rig() {
        api.arg = [](void*, const HailstoneArgs* args, std::size_t index, HailstoneValue* out) {
            if (index >= args->values.size()) return 0;
            *out = {};
            out->kind = HAILSTONE_STRING;
            out->s = args->values[index];
            return 1;
        };
        api.out_begin_table = [](void*, HailstoneOut* out) {
            out->parents.push_back(&out->put(Json::object()));
        };
        api.out_begin_array = [](void*, HailstoneOut* out) {
            out->parents.push_back(&out->put(Json::array()));
        };
        api.out_key = [](void*, HailstoneOut* out, const char* key) { out->key = key; };
        api.out_end = [](void*, HailstoneOut* out) { out->parents.pop_back(); };
        api.out_value = [](void*, HailstoneOut* out, const HailstoneValue* value) {
            if (value->kind == HAILSTONE_INT) out->put(value->i);
            if (value->kind == HAILSTONE_BOOL) out->put(value->i != 0);
            if (value->kind == HAILSTONE_STRING) out->put(value->s);
        };
        api.inspect_item = [](void* host, std::uint32_t id, const std::uint32_t* augments) {
            check(augments == nullptr, "a journal preview supplied augments");
            static_cast<Rig*>(static_cast<citadel_test::Host*>(host))->inspected.push_back(id);
            return 1;
        };
        journal.set_host(&api);
        journal.initialize();
    }


    Json call(std::initializer_list<const char*> args, bool valid = true) {
        const auto& fn = functions.at("expedition_journal");
        HailstoneArgs input{args};
        HailstoneOut output;
        check((fn.fn(fn.self, &input, &output) != 0) == valid, "unexpected UI operation result");
        check(output.parents.empty(), "unbalanced output table");
        return std::move(output.value);
    }

    void open() {
        journal.peer(true);
        journal.game_state(GAMESTATE_INGAME);
        check(journal.open(), "journal required a browser or refused the selected world");
        journal.pulse();
    }

    Bytes reply(std::string_view mode, const Json& state) {
        hailstone::ByteReader request(sent.back().bytes);
        check(sent.back().opcode == kOpcode && request.le<std::uint32_t>() == kMagic,
              "request opcode or magic changed");
        const std::string body = Json{{"type", "expeditionJournalStatus"}, {"version", 1},
                                      {"mode", mode}, {"state", state}}.dump();
        hailstone::ByteWriter writer;
        writer.le(kMagic);
        writer.le(request.le<std::uint16_t>());
        writer.le(request.le<std::uint16_t>());
        writer.le(request.le<std::uint32_t>());
        writer.le(static_cast<std::uint32_t>(body.size()));
        auto bytes = writer.take();
        for (char c : body) bytes.push_back(std::byte(static_cast<unsigned char>(c)));
        return bytes;
    }

    void receive(const Bytes& bytes) {
        check(journal.packet(kOpcode, bytes.data(), bytes.size()), "plugin did not consume the journal opcode");
    }
};

void snapshots() {
    Rig rig;
    check(!rig.journal.open(), "opened on another world");
    rig.open();
    auto window = rig.call({"window"});
    check(window["shown"] == true && window["connected"] == true && window["character"] == "Fixture",
          "journal did not expose its native session");
    check(rig.sent.size() == 1 && rig.sent.back().bytes.size() == 16, "default request was missing or wrong size");
    check(rig.call({"snapshot", "normal"})["status"] == "loading", "request was not marked pending");
    const Json normal = {
        {"serverNow", 1700000000},
        {"run", {{"state", "rejoin"}, {"sealed", true}, {"expiresAt", 1700000050}}},
        {"replay", {{"state", "locked"}, {"expiresAt", 1700000500}}},
        {"roster", {{"state", "eligible"}, {"members", Json::array({{{"name", "Fixture"}, {"state", "eligible"}}})}}},
    };
    const auto reply = rig.reply("normal", normal);
    auto forged = reply;
    citadel_test::put(forged, 8, 0);
    rig.receive(forged);
    forged = reply;
    forged.pop_back();
    rig.receive(forged);
    rig.receive(rig.reply("hard", normal));
    check(rig.call({"snapshot", "normal"})["status"] == "loading", "mismatched reply published data");
    rig.receive(reply);
    auto snapshot = rig.call({"snapshot", "normal"});
    check(snapshot["status"] == "ready" && snapshot["data"] == normal,
          "native status altered saved-run, replay, or roster fields");
    check(rig.call({"window"})["revision"] != window["revision"], "native cache did not notify the UI");

    rig.call({"select", "hard", "170009"}, false);
    rig.call({"select", "copium"});
    rig.journal.pulse();
    check(rig.sent.size() == 1, "switching views bypassed the server request interval");
    rig.now += 1100;
    rig.journal.pulse();
    check(rig.sent.size() == 2, "native Copium list request was lost");
    const Json row{{"relicId", 170009}, {"name", "Jelvan"}, {"credit", "credited"}, {"quantity", 1}};
    rig.receive(rig.reply("copium", {{"relics", Json::array({row})}}));
    check(rig.call({"snapshot", "copium"})["data"]["relics"][0] == row, "Copium list was unavailable");
    rig.call({"select", "copium", "170009"});
    rig.now += 1100;
    rig.journal.pulse();
    hailstone::ByteReader detail(rig.sent.back().bytes);
    detail.skip(6);
    check(detail.le<std::uint16_t>() == 4, "Copium detail used the wrong mode");
    detail.skip(4);
    check(detail.le<std::uint32_t>() == 170009, "Copium detail lost the requested identity");
    rig.receive(rig.reply("copium", {{"relic", {{"relicId", 170010}}}}));
    check(rig.call({"snapshot", "copium", "170009"})["status"] == "loading",
          "wrong relic prematurely completed the pending request");
    rig.receive(rig.reply("copium", {{"relic", row}}));
    check(rig.call({"snapshot", "copium", "170009"})["data"]["relic"] == row,
          "selected relic data did not reach the native UI");
    check(rig.call({"snapshot", "copium"})["data"]["relics"][0] == row,
          "a relic detail invalidated its list");

    rig.call({"select", "sleepers", "170009"}, false);
    rig.call({"select", "sleepers"});
    rig.now += 1100;
    rig.journal.pulse();
    hailstone::ByteReader sleepers(rig.sent.back().bytes);
    sleepers.skip(6);
    check(sleepers.le<std::uint16_t>() == 5, "Sleeper phase used the wrong mode");
    sleepers.skip(4);
    check(sleepers.le<std::uint32_t>() == 0, "Sleeper phase sent a relic identifier");
    const Json phases{{"publicPhase", "2.0"}, {"nextPhase", "1.0"}, {"rotation", "alternating"},
                      {"savedRun", {{"phase", "2.0"}, {"expiresAt", 1700000400}}}};
    rig.receive(rig.reply("sleepers", phases));
    check(rig.call({"snapshot", "sleepers"})["data"] == phases, "Sleeper phases did not reach the native UI");
    check(rig.call({"snapshot", "copium", "170009"})["data"]["relic"] == row,
          "Sleeper status replaced the selected Copium relic cache");

    const char* featured[] = {"tofs_group", "tofs_raid", "broken_accord", "firefell"};
    for (std::uint16_t i = 0; i < std::size(featured); ++i) {
        const auto mode = featured[i];
        rig.call({"select", mode, "42"}, false);
        rig.call({"select", mode});
        rig.now += 1100;
        rig.journal.pulse();
        hailstone::ByteReader request(rig.sent.back().bytes);
        request.skip(6);
        check(request.le<std::uint16_t>() == 6 + i, "featured status request used the wrong mode");
        request.skip(4);
        check(request.le<std::uint32_t>() == 0, "featured status tried to scope another character");
        const Json status{{"character", "Fixture"}, {"run", {{"state", "rejoin"}}},
                          {"replay", {{"state", "locked"}, {"expiresAt", 1700000100 + i}}}};
        rig.receive(rig.reply(mode, status));
        check(rig.call({"snapshot", mode})["data"] == status, "featured live snapshot was lost");
    }
    check(rig.call({"snapshot", "tofs_group"})["data"]["replay"]["expiresAt"] == 1700000100,
          "switching featured pages overwrote another snapshot");
    check(rig.call({"snapshot", "copium", "170009"})["data"]["relic"] == row,
          "featured status overwrote the selected relic snapshot");

    rig.now += 30000;
    rig.journal.pulse();
    check(!rig.call({"snapshot", "normal"}).contains("data"), "expired availability was retained");
}

void lifecycle() {
    Rig rig;
    rig.open();
    const auto late = rig.reply("normal", {{"character", "Old"}});
    auto generation = rig.call({"window"})["generation"];
    rig.journal.begin_zone();
    rig.receive(late);
    check(rig.call({"window"})["connected"] == false, "zone transition exposed old context");
    check(rig.call({"window"})["generation"] != generation, "zone transition failed to retire UI state");
    rig.journal.game_state(GAMESTATE_INGAME);
    ++rig.incarnation;
    rig.journal.pulse();
    check(rig.sent.size() == 2, "window did not check for live data in the new zone");
    rig.receive(late);
    check(rig.call({"snapshot", "normal"})["status"] == "loading", "late reply survived zoning");
    rig.now += 6501;
    rig.journal.pulse();
    check(rig.call({"snapshot", "normal"})["status"] == "unavailable", "request lacked a timeout");
    rig.call({"refresh", "vigil"});
    rig.journal.pulse();
    check(rig.sent.size() == 3, "manual refresh did not send request");
    rig.call({"close"});
    rig.journal.pulse();
    check(rig.call({"window"})["shown"] == false && rig.sent.size() == 3,
          "closed journal polled the realm or stayed open");
}

void inspect_items() {
    Rig rig;
    rig.call({"inspect_item", "147760"}, false);
    rig.open();
    check(rig.call({"window"})["canInspectItem"] == true, "item inspection capability was not published");
    rig.call({"inspect_item", "147760"});
    rig.call({"inspect_item", "4294967296"}, false);
    rig.call({"inspect_item", "47283"}, false);
    rig.call({"select", "copium", "170009"});
    rig.now += 1100;
    rig.journal.pulse();
    const Json detail{{"relicId", 170009}, {"relicName", "Relic of the Tormentors"},
                      {"rewards", Json::array({{{"itemId", 47283}, {"name", "Girdle of the Fleet"}},
                                               {{"itemId", 47289}}})}};
    rig.receive(rig.reply("copium", {{"relic", detail}}));
    rig.call({"inspect_item", "170009"});
    rig.call({"inspect_item", "47283"});
    rig.call({"inspect_item", "47289"}, false);
    rig.journal.begin_zone();
    rig.call({"inspect_item", "47283"}, false);
    check(rig.inspected == std::vector<std::uint32_t>{147760, 170009, 47283},
          "inspection forwarded a hidden, expired, or out-of-scope item");
}
} // namespace

int main() {
    try {
        snapshots();
        lifecycle();
        inspect_items();
        std::cout << citadel_test::checks << " native expedition journal checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
