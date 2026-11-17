#include "follow_addon.h"
#include "citadel_services.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <vector>

namespace bastion::follow {
namespace {
std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}
std::vector<std::string_view> words(std::string_view text) {
    std::vector<std::string_view> out;
    while (!(text = trim(text)).empty() && out.size() < 8) {
        const auto end = text.find_first_of(" \t");
        out.push_back(text.substr(0, end));
        if (end == std::string_view::npos) return out;
        text.remove_prefix(end);
    }
    if (!text.empty()) out.clear();
    return out;
}
std::optional<int> integer(std::string_view text) {
    int n = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), n);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() ? std::optional(n) : std::nullopt;
}
void field(const PluginHostApi& h, HailstoneOut* out, const char* key, double number) {
    h.out_key(h.host, out, key); HailstoneValue value{}; value.kind = HAILSTONE_FLOAT; value.f = number;
    h.out_value(h.host, out, &value);
}
void field(const PluginHostApi& h, HailstoneOut* out, const char* key, const std::string& text) {
    h.out_key(h.host, out, key); HailstoneValue value{}; value.kind = HAILSTONE_STRING; value.s = text.c_str();
    h.out_value(h.host, out, &value);
}
std::string argument(const PluginHostApi& h, const HailstoneArgs* args, std::size_t at) {
    HailstoneValue value{};
    if (!h.arg(h.host, args, at, &value) || value.kind != HAILSTONE_STRING || !value.s) return {};
    std::size_t size = 0; while (size <= 128 && value.s[size]) ++size;
    return size <= 128 ? std::string(value.s, size) : std::string{};
}
} // namespace

std::optional<int> parse_distance(std::string_view text) {
    text = trim(text);
    if (text.empty() || text.size() > 6) return {};
    const auto dot = text.find('.');
    auto whole = integer(text.substr(0, dot));
    if (!whole || *whole < 0 || *whole > 50) return {};
    int cents = 0;
    if (dot != std::string_view::npos) {
        const auto fraction = text.substr(dot + 1);
        if (fraction.empty() || fraction.size() > 2) return {};
        for (char c : fraction) if (c < '0' || c > '9') return {};
        cents = fraction[0] - '0';
        cents = cents * 10 + (fraction.size() == 2 ? fraction[1] - '0' : 0);
    }
    const int total = *whole * 100 + cents;
    return total >= 100 && total <= 5000 ? std::optional(total) : std::nullopt;
}

std::string Settings::serialize() const {
    return "BASTION_FOLLOW1\nenabled=" + std::to_string(enabled ? 1 : 0) +
           "\ncancel_on_move=" + std::to_string(cancel_on_move ? 1 : 0) +
           "\ninstant_mount=" + std::to_string(instant_mount ? 1 : 0) +
           "\ndistance_hundredths=" + std::to_string(distance_hundredths) + "\n";
}
std::optional<Settings> Settings::parse(std::string_view text) {
    if (text.size() > 4096 || text.find('\0') != std::string_view::npos || !text.starts_with("BASTION_FOLLOW1\n")) return {};
    text.remove_prefix(16);
    Settings result;
    unsigned seen = 0;
    while (!text.empty()) {
        const auto end = text.find('\n'); const auto row = trim(text.substr(0, end));
        if (end == std::string_view::npos) text = {}; else text.remove_prefix(end + 1);
        if (row.empty()) continue;
        const auto equals = row.find('=');
        if (equals == std::string_view::npos) return {};
        const auto key = trim(row.substr(0, equals)); const auto n = integer(trim(row.substr(equals + 1)));
        if (!n) return {};
        unsigned bit = 0;
        if (key == "enabled") { bit = 1; if (*n < 0 || *n > 1) return {}; result.enabled = *n != 0; }
        else if (key == "cancel_on_move") { bit = 2; if (*n < 0 || *n > 1) return {}; result.cancel_on_move = *n != 0; }
        else if (key == "instant_mount") { bit = 4; if (*n < 0 || *n > 1) return {}; result.instant_mount = *n != 0; }
        else if (key == "distance_hundredths") { bit = 8; if (*n < 100 || *n > 5000) return {}; result.distance_hundredths = *n; }
        else return {};
        if (seen & bit) return {};
        seen |= bit;
    }
    return result;
}

HailstoneFollowProfile profile(const Settings& settings) {
    HailstoneFollowProfile out{}; out.size = sizeof(out);
    out.flags = HAILSTONE_FOLLOW_ALLOW_UNGROUPED | HAILSTONE_FOLLOW_LOOK_PITCH | HAILSTONE_FOLLOW_PRESERVE_RUN |
                (settings.cancel_on_move ? HAILSTONE_FOLLOW_CANCEL_ON_MOVE : 0u);
    out.stop_distance = static_cast<float>(settings.distance_hundredths) / 100.0f;
    out.restart_margin = 3; out.start_range = 150;
    out.turn_snap = 8; out.turn_slow = 12; out.turn_fast = 20; out.turn_fast_angle = 64;
    out.move_angle = 96; out.move_far_margin = 15;
    out.pitch_gain = .1f; out.pitch_down_scale = .75f; out.pitch_limit = 64;
    return out;
}

void Addon::set_host(const PluginHostApi* host) {
    if (HAILSTONE_HOST_HAS(host_, remove_ui_function)) host_->remove_ui_function(host_->host, "citadel_follow");
    host_ = host; selected_ = false; directory_cache_.reset();directory_.clear(); settings_ = {}; error_.clear();
}
void Addon::initialize() {
    if (HAILSTONE_HOST_HAS(host_, add_ui_function) && HAILSTONE_HOST_HAS(host_, arg) && HAILSTONE_HOST_HAS(host_, out_end))
        host_->add_ui_function(host_->host, "citadel_follow", ui_callback, this);
}
bool Addon::ready() const {
    HailstonePlayer player{}; player.size = sizeof(player);
    return selected_ && HAILSTONE_HOST_HAS(host_, get_player) && host_->get_player(host_->host, &player) &&
           player.size >= offsetof(HailstonePlayer, in_zone) + sizeof(player.in_zone) && player.in_zone == 1 && player.spawn_id;
}
void Addon::peer(bool selected) { directory_cache_.reset();selected_ = selected; directory_.clear(); settings_ = {}; error_.clear(); refresh_settings(); }
void Addon::game_state(int) { directory_cache_.reset();refresh_settings(); }
void Addon::begin_zone() { directory_cache_.reset();directory_.clear(); settings_ = {}; error_.clear(); }
void Addon::pulse() { refresh_settings(); }
void Addon::refresh_settings() {
    if (!ready()) { directory_.clear(); settings_ = {}; return; }
    const auto directory=directory_cache_.get(host_);
    if (directory.empty()) {
        directory_.clear(); settings_ = {}; error_ = "Writable character settings are unavailable."; return;
    }
    if (directory == directory_) return;
    directory_ = directory; settings_ = {}; error_.clear();
    std::error_code ec;
    const auto file = directory_ / "follow.cfg";
    if (!std::filesystem::exists(file, ec) && !ec) return;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size > 4096) { error_ = "Follow settings could not be read."; return; }
    std::ifstream input(file, std::ios::binary); std::string plain(static_cast<std::size_t>(size), '\0');
    if (!input.read(plain.data(), static_cast<std::streamsize>(plain.size()))) { error_ = "Follow settings could not be read."; return; }
    if (auto parsed = Settings::parse(plain)) settings_ = *parsed; else error_ = "Invalid follow settings; defaults are active.";
}
bool Addon::save(const Settings& replacement) {
    if (!ready() || directory_.empty()) { error_ = "Connect to the selected server and writable character settings first."; return false; }
    static std::uint64_t sequence = 0;
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." + std::to_string(++sequence);
    const auto temporary = directory_ / ("follow.pending." + suffix), target = directory_ / "follow.cfg", backup = directory_ / ("follow.previous." + suffix);
    std::error_code ec; std::filesystem::create_directories(directory_, ec);
    if (ec) { error_ = "Follow settings directory is not writable."; return false; }
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    const auto plain = replacement.serialize(); output.write(plain.data(), static_cast<std::streamsize>(plain.size())); output.flush(); output.close();
    if (!output) { std::filesystem::remove(temporary, ec); error_ = "Follow settings could not be saved."; return false; }
    const bool previous = std::filesystem::exists(target, ec);
    if (!ec && previous) std::filesystem::rename(target, backup, ec);
    if (!ec) std::filesystem::rename(temporary, target, ec);
    if (ec) {
        std::error_code ignored;
        if (previous && std::filesystem::exists(backup, ignored)) std::filesystem::rename(backup, target, ignored);
        std::filesystem::remove(temporary, ignored); error_ = "Follow settings could not be replaced; previous settings are preserved."; return false;
    }
    if (previous) std::filesystem::remove(backup, ec);
    settings_ = replacement; error_.clear(); return true;
}
bool Addon::change(std::string_view key, double value) {
    auto invalid = [&] { error_ = "Follow setting is unknown or outside its allowed range."; return false; };
    if (!std::isfinite(value)) return invalid();
    Settings replacement = settings_;
    if (key == "distance") {
        if (value < 1 || value > 50) return invalid();
        replacement.distance_hundredths = static_cast<int>(std::round(value * 100));
    } else {
        if (value != 0 && value != 1) return invalid();
        if (key == "enabled") replacement.enabled = value != 0;
        else if (key == "cancel_on_move") replacement.cancel_on_move = value != 0;
        else if (key == "instant_mount") replacement.instant_mount = value != 0;
        else return invalid();
    }
    return save(replacement);
}
int Addon::follow_profile(HailstoneFollowProfile* out) const {
    if (!out || out->size < sizeof(*out) || !ready() || !settings_.enabled) return 0;
    *out = profile(settings_); return 1;
}
int Addon::mount_profile(HailstoneMountProfile* out) const {
    if (!out || out->size < sizeof(*out) || !ready()) return 0;
    *out = {sizeof(*out), settings_.instant_mount ? 1u : 0u}; return 1;
}
bool Addon::control(const char* action) {
    const bool okay = ready() && HAILSTONE_HOST_HAS(host_, manual_follow) && host_->manual_follow(host_->host, action);
    if (!okay) error_ = "Follow was refused. Enable enhanced follow and select a nearby living player, or use Stop.";
    else error_.clear();
    return okay;
}
void Addon::message(std::string_view text) const {
    if (HAILSTONE_HOST_HAS(host_, write_chat)) { const std::string copy(text); host_->write_chat(host_->host, copy.c_str(), 0); }
}
bool Addon::command(std::string_view line) {
    if (!selected_ || line.size() > 256 || line.find_first_of("\r\n") != std::string_view::npos) return false;
    std::string folded(line);
    for (auto& c : folded) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    line = folded;
    refresh_settings();
    auto parts = words(line); if (parts.empty()) return false;
    bool mount = false;
    if (parts[0] == "/citadel" && parts.size() >= 2 && (parts[1] == "follow" || parts[1] == "mount")) {
        mount = parts[1] == "mount"; parts.erase(parts.begin(), parts.begin() + 2);
        if (mount) {
            if (parts.size() > 1) { message("Use /citadel mount on|off|toggle."); return true; }
            const auto action = parts.empty() ? std::string_view("toggle") : parts[0];
            const bool value = action == "on" || (action == "toggle" && !settings_.instant_mount);
            if (action != "on" && action != "off" && action != "toggle") message("Use /citadel mount on|off|toggle.");
            else message(change("instant_mount", value ? 1 : 0) ? "Instant mount acceleration setting saved." : error_);
            return true;
        }
        if (parts.empty()) { control("toggle"); return true; }
    } else if (parts[0] == "/follow" && parts.size() >= 2) {
        parts.erase(parts.begin());
        if (parts[0] == "on" || parts[0] == "off") {
            if (parts.size() == 1) control(parts[0] == "on" ? "start" : "stop");
            else message("Use /follow on|off.");
            return true;
        }
        if (parts[0] == "citadel") parts.erase(parts.begin());
        else if (parts[0] != "distance") return false;
    } else return false;
    bool okay = false;
    if (parts.size() == 1 && (parts[0] == "on" || parts[0] == "off")) okay = change("enabled", parts[0] == "on" ? 1 : 0);
    else if (!parts.empty() && parts[0] == "cancel" && parts.size() <= 2) {
        const auto action = parts.size() == 2 ? parts[1] : std::string_view("toggle");
        if (action == "on" || action == "off" || action == "toggle")
            okay = change("cancel_on_move", action == "on" || (action == "toggle" && !settings_.cancel_on_move) ? 1 : 0);
    } else if (parts.size() == 2 && parts[0] == "distance") {
        if (const auto cents = parse_distance(parts[1])) okay = change("distance", static_cast<double>(*cents) / 100);
    } else { message("Use /follow citadel on|off, /follow distance <1..50>, /follow citadel cancel on|off, /follow on|off."); return true; }
    message(okay ? "Follow setting saved." : error_.empty() ? "Invalid follow setting." : error_);
    return true;
}

int Addon::ui_callback(void* self, const HailstoneArgs* args, HailstoneOut* out) noexcept {
    if (!self) return 0;
    auto &owner = *static_cast<Addon*>(self);
    return services::guarded_ui(owner.host_, [&] { return owner.ui(args,out); });
}
int Addon::ui(const HailstoneArgs* args, HailstoneOut* out) {
    if (!host_ || !out) return 0;
    refresh_settings();
    const auto& h = *host_; const auto operation = argument(h, args, 0);
    bool okay = true;
    if (operation == "set") {
        HailstoneValue v{}; const bool found = h.arg(h.host, args, 2, &v) != 0;
        const double n = v.kind == HAILSTONE_INT || v.kind == HAILSTONE_BOOL ? static_cast<double>(v.i) : v.f;
        okay = found && (v.kind == HAILSTONE_INT || v.kind == HAILSTONE_BOOL || v.kind == HAILSTONE_FLOAT) && change(argument(h, args, 1), n);
    } else if (operation == "start" || operation == "stop" || operation == "toggle") okay = control(operation.c_str());
    else if (operation != "state") okay = false;
    h.out_begin_table(h.host, out);
    field(h, out, "ok", okay ? 1.0 : 0.0); field(h, out, "active", ready() ? 1.0 : 0.0);
    field(h, out, "enabled", settings_.enabled ? 1.0 : 0.0); field(h, out, "cancel_on_move", settings_.cancel_on_move ? 1.0 : 0.0);
    field(h, out, "instant_mount", settings_.instant_mount ? 1.0 : 0.0);
    field(h, out, "distance", static_cast<double>(settings_.distance_hundredths) / 100); field(h, out, "error", error_);
    HailstonePlayer player{}; player.size = sizeof(player);
    HailstoneSpawn target{}; target.size = sizeof(target);
    HailstoneSpawn self{}; self.size = sizeof(self);
    bool can_start = false;
    if (ready() && HAILSTONE_HOST_HAS(host_, get_spawn) && h.get_player(h.host, &player) && player.target_id &&
        h.get_spawn(h.host, player.target_id, &target) && target.id == player.target_id &&
        h.get_spawn(h.host, player.spawn_id, &self) && self.id == player.spawn_id) {
        const auto distance = std::hypot(self.x - target.x, self.y - target.y);
        can_start = settings_.enabled && !target.is_npc && !target.is_corpse && target.id != player.spawn_id &&
                    std::isfinite(distance) && distance <= 150;
        std::size_t length = 0; while (length < sizeof(target.name) && target.name[length]) ++length;
        field(h, out, "selected_name", std::string(target.name, length));
        if (std::isfinite(distance)) field(h, out, "selected_distance", static_cast<double>(distance));
    }
    HailstoneFollowStatus status{}; status.size = sizeof(status);
    if (HAILSTONE_HOST_HAS(host_, get_follow_status) && h.get_follow_status(h.host, &status)) {
        if (status.active) can_start = false;
        field(h, out, "following", status.active ? 1.0 : 0.0); field(h, out, "enhanced", status.enhanced ? 1.0 : 0.0);
        field(h, out, "target_id", static_cast<double>(status.target_id));
        HailstoneSpawn followed{}; followed.size = sizeof(followed);
        if (status.active && HAILSTONE_HOST_HAS(host_, get_spawn) && h.get_spawn(h.host, status.target_id, &followed) &&
            followed.id == status.target_id && followed.size >= offsetof(HailstoneSpawn, incarnation) + sizeof(followed.incarnation) &&
            followed.incarnation == status.target_incarnation) {
            std::size_t length = 0; while (length < sizeof(followed.name) && followed.name[length]) ++length;
            field(h, out, "following_name", std::string(followed.name, length));
        }
    }
    field(h, out, "can_start", can_start ? 1.0 : 0.0);
    h.out_end(h.host, out); return 1;
}
} // namespace bastion::follow
