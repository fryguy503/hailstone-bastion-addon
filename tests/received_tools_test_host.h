#pragma once
#include "citadel_test_host.h"
#include <iostream>

struct HailstoneArgs { std::vector<HailstoneValue> values; };
struct HailstoneOut {
    std::string key;
    std::map<std::string, std::int64_t> numbers;
    unsigned depth = 0, tables = 0, arrays = 0, values = 0;
};

namespace citadel_test {
struct ReceivedHost : Host {
    unsigned player_reads = 0, spawn_reads = 0, item_reads = 0, clock_reads = 0, directory_reads = 0;
    unsigned visibility_queries = 0;
    decltype(PluginHostApi::get_player) original_player = nullptr;
    decltype(PluginHostApi::get_spawn) original_spawn = nullptr;
    ReceivedHost() {
        original_player = api.get_player;
        original_spawn = api.get_spawn;
        api.get_player = [](void* self, HailstonePlayer* out) {
            auto& h = *static_cast<ReceivedHost*>(self);
            ++h.player_reads;
            return h.original_player(self, out);
        };
        api.get_spawn = [](void* self, std::uint32_t id, HailstoneSpawn* out) {
            auto& h = *static_cast<ReceivedHost*>(self);
            ++h.spawn_reads;
            return h.original_spawn(self, id, out);
        };
        api.get_item = [](void* self, const HailstoneItemSlot*, HailstoneItem*) {
            ++static_cast<ReceivedHost*>(self)->item_reads;
            return 0;
        };
        api.monotonic_ms = [](void* self) {
            auto& h = *static_cast<ReceivedHost*>(self);
            ++h.clock_reads;
            return h.now;
        };
        api.config_dir = api.character_config_dir = [](void* self, char*, std::size_t) {
            ++static_cast<ReceivedHost*>(self)->directory_reads;
            return 0;
        };
        api.arg = [](void*, const HailstoneArgs* args, std::size_t index, HailstoneValue* out) {
            if (index >= args->values.size()) return 0;
            *out = args->values[index];
            return 1;
        };
        api.out_begin_table = [](void*, HailstoneOut* out) { ++out->tables; ++out->depth; };
        api.out_begin_array = [](void*, HailstoneOut* out) { ++out->arrays; ++out->depth; };
        api.out_key = [](void*, HailstoneOut* out, const char* key) { out->key = key; };
        api.out_value = [](void*, HailstoneOut* out, const HailstoneValue* value) {
            ++out->values;
            if (out->depth == 1 && (value->kind == HAILSTONE_INT || value->kind == HAILSTONE_BOOL))
                out->numbers[out->key] = value->i;
        };
        api.out_end = [](void*, HailstoneOut* out) {
            check(out->depth > 0, "output scope underflow");
            --out->depth;
        };
    }
    unsigned reads() const { return player_reads + spawn_reads + item_reads + clock_reads + directory_reads; }
    HailstoneOut call(const char* feature, const char* operation) {
        HailstoneValue value{};
        value.kind = HAILSTONE_STRING;
        value.s = operation;
        HailstoneArgs args{{value}};
        HailstoneOut out;
        const auto function = functions.at(feature);
        check(function.fn(function.self, &args, &out) == 1 && out.depth == 0, "received tool UI query failed");
        return out;
    }
    void visibility(const char* feature, bool active, bool shown, unsigned frames = 120) {
        const auto before = reads();
        const auto packets = sent.size();
        const auto actions = commands.size();
        for (unsigned frame = 0; frame < frames; ++frame) {
            const auto out = call(feature, "visibility");
            ++visibility_queries;
            check(out.tables == 1 && out.arrays == 0 && out.values == 2 && out.numbers.size() == 2,
                  "visibility enumerated a catalog, slots or full state");
            check(out.numbers.at("active") == (active ? 1 : 0) && out.numbers.at("shown") == (shown ? 1 : 0),
                  "visibility did not follow current lifecycle");
        }
        check(reads() == before && sent.size() == packets && commands.size() == actions,
              "visibility performed game, cursor, clock, file or network work");
    }
};

template<class Adapter> void received_visibility(const char* feature) {
    ReceivedHost h;
    Adapter adapter;
    adapter.set_host(&h.api);
    adapter.initialize();
    h.visibility(feature, false, false);
    adapter.peer(true);
    adapter.game_state(GAMESTATE_INGAME);
    h.visibility(feature, true, false);
    check(adapter.command("open"), "native command did not open received tool");
    h.visibility(feature, true, true);
    const auto reads = h.reads();
    const auto state = h.call(feature, "state");
    check(state.arrays > 0 && h.reads() > reads, "full state lost fresh validation or received arrays");
    std::cout << feature << " full-state sample: " << h.reads() - reads << " host reads, "
              << state.arrays << " arrays, " << state.values << " output values\n";
    h.call(feature, "close");
    h.visibility(feature, true, false);
    check(adapter.command("open"), "native command did not reopen received tool");
    ++h.incarnation;
    adapter.pulse();
    h.visibility(feature, true, false);
    check(adapter.command("open"), "fresh owner did not reopen received tool");
    adapter.begin_zone();
    h.visibility(feature, false, false);
    adapter.game_state(GAMESTATE_INGAME);
    h.visibility(feature, true, false);
    adapter.peer(false);
    h.visibility(feature, false, false);
    adapter.shutdown();
    check(!h.functions.contains(feature), "received visibility callback leaked on shutdown");
    std::cout << feature << " visibility: " << h.visibility_queries
              << " queries, 0 game/cursor/clock/directory reads, 0 arrays, 0 requests\n";
}
} // namespace citadel_test
