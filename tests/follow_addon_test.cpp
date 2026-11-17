#include "follow_addon.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

struct HailstoneArgs { std::vector<HailstoneValue> values; };
struct HailstoneOut { std::string key; std::map<std::string, double> numbers; std::map<std::string, std::string> strings; };
namespace {
unsigned checks = 0;
void check(bool condition, const char* why) { ++checks; if (!condition) throw std::runtime_error(why); }
struct Scratch {
    std::filesystem::path path;
    Scratch() {
        static unsigned serial = 0;
        path = std::filesystem::temp_directory_path() / ("bastion-follow-test-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++serial));
        std::filesystem::create_directories(path);
    }
    ~Scratch() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
struct Host {
    PluginHostApi api{};
    Scratch scratch;
    bool in_zone = true, manual_scope = false;
    HailstoneUiFn function = nullptr; void* user = nullptr;
    std::vector<std::string> actions;
    Host() {
        api.abi = HAILSTONE_PLUGIN_ABI; api.size = sizeof(api); api.host = this;
        api.get_player = [](void* context, HailstonePlayer* out) {
            const auto& self = *static_cast<Host*>(context);
            *out = {}; out->size = sizeof(*out); out->spawn_id = 1; out->in_zone = self.in_zone ? 1 : 0; return 1;
        };
        api.character_config_dir = [](void* context, char* out, std::size_t capacity) {
            const auto text = static_cast<Host*>(context)->scratch.path.string();
            if (capacity <= text.size()) return -1;
            std::memcpy(out, text.c_str(), text.size() + 1); return static_cast<int>(text.size());
        };
        api.manual_follow = [](void* context, const char* action) {
            auto& self = *static_cast<Host*>(context);
            if (!self.manual_scope) return 0;
            self.actions.emplace_back(action); return 1;
        };
        api.add_ui_function = [](void* context, const char*, HailstoneUiFn fn, void* callback_user) {
            auto& self = *static_cast<Host*>(context); self.function = fn; self.user = callback_user; return 1;
        };
        api.remove_ui_function = [](void*, const char*) {};
        api.arg = [](void*, const HailstoneArgs* args, std::size_t index, HailstoneValue* out) {
            if (index >= args->values.size()) return 0;
            *out = args->values[index]; return 1;
        };
        api.out_begin_table = [](void*, HailstoneOut*) {};
        api.out_key = [](void*, HailstoneOut* out, const char* key) { out->key = key; };
        api.out_value = [](void*, HailstoneOut* out, const HailstoneValue* value) {
            if (value->kind == HAILSTONE_STRING) out->strings[out->key] = value->s;
            else out->numbers[out->key] = value->kind == HAILSTONE_FLOAT ? value->f : static_cast<double>(value->i);
        };
        api.out_end = [](void*, HailstoneOut*) {};
    }
    HailstoneOut call(const char* operation, const char* key = nullptr, double number = 0) {
        HailstoneArgs args; HailstoneValue value{}; value.kind = HAILSTONE_STRING; value.s = operation; args.values.push_back(value);
        if (key) { value.s = key; args.values.push_back(value); value.kind = HAILSTONE_FLOAT; value.f = number; args.values.push_back(value); }
        HailstoneOut out; check(function && function(user, &args, &out) == 1, "actual SDK UI callback"); return out;
    }
};
}
int main() {
    try {
        namespace follow = bastion::follow;
        follow::Settings defaults;
        check(!defaults.enabled && defaults.cancel_on_move && defaults.instant_mount && defaults.distance_hundredths == 1500, "native settings defaults");
        for (int distance : {100, 1500, 5000}) {
            auto settings = defaults; settings.enabled = true; settings.distance_hundredths = distance;
            const auto parsed = follow::Settings::parse(settings.serialize());
            check(parsed && *parsed == settings, "settings roundtrip");
            const auto profile = follow::profile(settings);
            check(profile.size == sizeof(profile) && profile.stop_distance == static_cast<float>(distance) / 100, "profile distance");
            check((profile.flags & HAILSTONE_FOLLOW_PRESERVE_RUN) != 0 && profile.start_range == 150, "manual profile limits");
        }
        for (auto bad : {"", "1.234", "nan", "inf", "0", "50.01", "-1", "15x", "1e1", "15\n"}) check(!follow::parse_distance(bad), "reject malformed distance");
        check(follow::parse_distance("1.01") == 101 && follow::parse_distance("50") == 5000, "valid decimal distance");
        auto malformed = defaults.serialize() + "enabled=1\n";
        check(!follow::Settings::parse(malformed), "duplicate settings rejected atomically");
        check(!follow::Settings::parse(defaults.serialize() + "command=/follow\n"), "no imported commands");
        check(!follow::Settings::parse(std::string(4097, 'x')), "bounded config");
        check(!follow::Settings::parse(defaults.serialize() + std::string(1, '\0')), "NUL rejected");

        Host host; follow::Addon addon; addon.set_host(&host.api); addon.initialize();
        HailstoneFollowProfile p{}; p.size = sizeof(p); HailstoneMountProfile m{}; m.size = sizeof(m);
        check(!addon.follow_profile(&p) && !addon.mount_profile(&m), "unselected peer exposes no profiles");
        addon.peer(true); addon.game_state(GAMESTATE_INGAME); addon.pulse();
        check(!addon.follow_profile(&p) && addon.mount_profile(&m) && m.instant_acceleration == 1, "stock follow until opt in");
        check(addon.command("/FOLLOW CITADEL ON"), "case insensitive manual setting");
        check(addon.follow_profile(&p) == 1 && host.actions.empty(), "settings never start a follow");
        check(addon.command("/follow distance 23.25") && addon.settings().distance_hundredths == 2325, "distance command");
        check(addon.command("/citadel follow cancel off") && !addon.settings().cancel_on_move, "cancel compatibility alias");
        check(addon.command("/citadel mount off") && !addon.settings().instant_mount, "mount setting");
        check(addon.command("/citadel follow scan") && host.actions.empty(), "legacy native scan cannot inject actions");
        check(!addon.command("/nav target") && !addon.command("/follow someone"), "unrelated commands untouched");
        check(addon.command("/follow on") && host.actions.empty(), "host refuses nonmanual action context");
        host.manual_scope = true; check(addon.command("/follow on") && host.actions.size() == 1 && host.actions[0] == "start", "typed manual start only");
        auto state = host.call("state"); check(state.numbers["distance"] == 23.25 && state.numbers["enabled"] == 1, "UI settings state");
        check(host.call("set", "distance", std::numeric_limits<double>::infinity()).numbers["ok"] == 0, "UI finite validation");
        check(host.call("set", "distance", 51).numbers["ok"] == 0, "UI range validation");
        check(host.call("set", "enabled", 0).numbers["ok"] == 1 && !addon.follow_profile(&p), "UI disables profile");
        check(host.call("stop").numbers["ok"] == 1 && host.actions.back() == "stop", "UI typed stop");
        addon.command("/follow citadel on");
        follow::Addon reload; reload.set_host(&host.api); reload.peer(true); reload.game_state(GAMESTATE_INGAME);
        check(reload.settings().enabled && reload.settings().distance_hundredths == 2325 && !reload.settings().instant_mount, "per character persisted reload");
        p.size = sizeof(p) - 1; check(!reload.follow_profile(&p), "short profile buffers refused");
        m.size = sizeof(m) - 1; check(!reload.mount_profile(&m), "short mount buffers refused");
        check(!reload.follow_profile(nullptr) && !reload.mount_profile(nullptr), "null profile buffers refused");
        host.in_zone = false; p.size = sizeof(p); m.size = sizeof(m);
        check(!reload.follow_profile(&p) && !reload.mount_profile(&m), "retired zone cannot supply profiles");
        reload.peer(false); host.in_zone = true;
        check(!reload.follow_profile(&p) && !reload.mount_profile(&m), "server retirement");
        std::cout << "Follow addon: " << checks << " checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
