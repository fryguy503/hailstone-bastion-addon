// SPDX-License-Identifier: MIT
#include "tradeskill_plans.h"
#include "citadel_test_host.h"

#include <chrono>
#include <iostream>

struct HailstoneArgs { std::vector<std::string> values; };
struct HailstoneOut { std::string key; bool accepted = false; };

namespace {
using namespace bastion::tradeskill_plans;
using citadel_test::check;
using Json = nlohmann::json;

void fragments() {
    const std::string text(kFragment * 2 + 17, 'X');
    const auto first = encode(9, 200, text, 0);
    const auto second = encode(9, 200, text, kFragment);
    const auto last = encode(9, 200, text, kFragment * 2);
    check(first.size() == kHeader + kFragment && last.size() == kHeader + 17, "wrong fragment size");
    check(!decode(first.data(), first.size() - 1), "accepted a truncated frame");
    check(encode(0, 200, text, 0).empty() && encode(9, 200, text, 1).empty(), "encoded invalid header");
    Reply assembly;
    check(!assembly.append(*decode(last.data(), last.size())), "accepted a fragment before the first one");
    check(assembly.append(*decode(first.data(), first.size())), "rejected first fragment");
    check(!assembly.append(*decode(first.data(), first.size())), "accepted a repeated fragment");
    check(assembly.append(*decode(second.data(), second.size())) && !assembly.complete(), "completed early");
    check(assembly.append(*decode(last.data(), last.size())) && assembly.complete() && assembly.body == text,
          "fragment round trip failed");
}

struct Rig : citadel_test::Host {
    Adapter adapter;
    std::filesystem::path dir;

    Rig() {
        dir = std::filesystem::current_path() / ("tradeskill-test-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(dir);
        api.character_config_dir = [](void *host, char *buffer, std::size_t size) {
            const auto path = static_cast<Rig *>(static_cast<Host *>(host))->dir.string();
            if (path.size() >= size) return 0;
            std::memcpy(buffer, path.c_str(), path.size() + 1);
            return static_cast<int>(path.size());
        };
        api.arg = [](void *, const HailstoneArgs *args, std::size_t index, HailstoneValue *out) {
            if (index >= args->values.size()) return 0;
            out->kind = HAILSTONE_STRING;
            out->s = args->values[index].c_str();
            return 1;
        };
        api.out_begin_table = api.out_begin_array = api.out_end = [](void *, HailstoneOut *) {};
        api.out_key = [](void *, HailstoneOut *out, const char *key) { out->key = key; };
        api.out_value = [](void *, HailstoneOut *out, const HailstoneValue *value) {
            if (out->key == "accepted") out->accepted = value->kind == HAILSTONE_BOOL && value->i == 1;
        };
        adapter.set_host(&api);
        adapter.initialize();
        adapter.peer(true);
        adapter.game_state(GAMESTATE_INGAME);
    }

    ~Rig() {
        std::error_code ignore;
        std::filesystem::remove(dir / "tradeskill-pending-scopeA.json", ignore);
        std::filesystem::remove(dir / "tradeskill-pending-scopeA.json.tmp", ignore);
        std::filesystem::remove(dir, ignore);
    }

    bool call(std::initializer_list<std::string> args) {
        const auto &function = functions.at("tradeskill_plans");
        const HailstoneArgs input{std::vector<std::string>(args)};
        HailstoneOut output;
        check(function.fn(function.self, &input, &output) == 1, "UI action returned no state");
        return output.accepted;
    }

    Chunk last() {
        const auto &packet = sent.back();
        check(packet.opcode == kOpcode, "wrong tradeskill opcode");
        return *decode(packet.bytes.data(), packet.bytes.size());
    }

    Json request() { return Json::parse(last().bytes); }

    void receive(std::uint16_t status, const Json &body) {
        const auto response = encode(last().request, status, body.dump(), 0);
        check(adapter.packet(kOpcode, response.data(), response.size()), "tradeskill response was not consumed");
    }
};

void saved_edits() {
    Rig rig;
    check(rig.adapter.open(), "could not open planner");
    check(rig.request()["action"] == "list", "opening did not request the shared list");
    rig.receive(200, {{"scope", "scopeA"}, {"plans", Json::array()}});
    check(rig.call({"preview", "2199", "12", "item-10"}), "draft preview rejected");
    check(rig.sent.size() == 1, "request was not deferred within relay window");
    rig.now += 275;
    rig.adapter.pulse();
    check(rig.request()["plan"]["quantity"] == 12 && rig.request()["plan"]["targetItemKey"] == "item-10", "draft lost its target");
    rig.receive(200, {{"scope", "scopeA"}, {"computed", Json::object()}});
    rig.now += 275;
    check(rig.call({"create", "2199", "12", "item-10"}), "create failed");
    const auto original = rig.request();
    const auto id = original["id"].get<std::string>();
    const auto file = rig.dir / "tradeskill-pending-scopeA.json";
    check(original["action"] == "create" && !id.empty() && std::filesystem::exists(file), "draft was not persisted before sending");

    rig.adapter.begin_zone();
    rig.adapter.game_state(GAMESTATE_INGAME);
    ++rig.incarnation;
    check(rig.adapter.open(), "could not reopen after zoning");
    rig.receive(200, {{"scope", "scopeA"}, {"plans", Json::array()}});
    rig.now += 275;
    rig.adapter.pulse();
    check(rig.request() == original, "retry lost the original idempotency ID or draft");
    const Json completed_plan{{"revision", 1}, {"archived", false},
        {"plan", {{"id", id}, {"recipeId", 2199}, {"quantity", 12}, {"completed", Json::array()}}},
        {"computed", {{"checklist", Json::array({{{"id", "recipe:2199"}}})}}}};
    rig.receive(200, {{"scope", "scopeA"}, {"record", completed_plan}});
    check(!std::filesystem::exists(file), "confirmed save left its retry behind");
    rig.now += 275;
    check(rig.call({"update", id, "1", "step", "recipe:2199", "1"}), "checklist update rejected");
    check(rig.request()["plan"]["completed"] == Json::array({"recipe:2199"}), "checklist update did not travel");
    rig.receive(409, {{"scope", "scopeA"}, {"record", completed_plan}});
    check(std::filesystem::exists(file), "conflict discarded the pending edit");
    check(rig.call({"discard_pending"}) && !std::filesystem::exists(file), "explicit discard did not clear pending edit");
    check(rig.call({"pin", id}), "could not pin current plan");
    rig.adapter.begin_zone();
    rig.adapter.game_state(GAMESTATE_INGAME);
    ++rig.incarnation;
    rig.adapter.pulse();
    check(rig.request()["action"] == "list", "pinned plan did not recheck account identity");
    rig.receive(200, {{"scope", "scopeA"}, {"plans", Json::array()}});
    rig.now += 275;
    rig.adapter.pulse();
    check(rig.request()["action"] == "get" && rig.request()["id"] == id, "verified pin did not reopen after zoning");
    rig.receive(200, {{"scope", "scopeA"}, {"record", completed_plan}});
    rig.now += 275;
    check(rig.call({"update", id, "1", "step", "recipe:2199", "1"}), "restored pin could not edit progress");
}
} // namespace

int main() {
    try {
        fragments();
        saved_edits();
        std::cout << citadel_test::checks << " tradeskill checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
