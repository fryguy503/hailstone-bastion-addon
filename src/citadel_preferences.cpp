#include "citadel_preferences.h"
#include "citadel_services.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>

namespace bastion {
void CharacterDirectory::reset() {
    ++generation_; scope_.reset(); directory_.clear();
}
std::optional<CharacterDirectory::Scope> CharacterDirectory::scope(const PluginHostApi* host) {
    HailstonePlayer player{}; player.size=sizeof(player);
    HailstoneServer server{}; server.size=sizeof(server);
    if (!host->get_player(host->host,&player) ||
        player.size<offsetof(HailstonePlayer,in_zone)+sizeof(player.in_zone) || !player.in_zone || !player.spawn_id ||
        !host->server_info(host->host,&server) || server.size<sizeof(server)) return {};
    std::uint64_t context=0;
    if (HAILSTONE_HOST_HAS(host,get_map_policy)) {
        HailstoneMapPolicy policy{};policy.size=sizeof(policy);
        if (!host->get_map_policy(host->host,&policy) ||
            policy.size<offsetof(HailstoneMapPolicy,context)+sizeof(policy.context) || !policy.context) return {};
        context=policy.context;
    }
    return Scope{services::bounded(player.name),services::bounded(server.name),services::bounded(server.host),
                 services::bounded(server.world),services::bounded(server.world_host),server.port,player.spawn_id,
                 player.zone_id,player.zone_instance,context};
}
std::filesystem::path CharacterDirectory::get(const PluginHostApi* host) {
    if (host_!=host) { reset(); host_=host; }
    if (resolving_ || !HAILSTONE_HOST_HAS(host,character_config_dir)) return {};
    resolving_=true;
    struct Finish { bool& resolving; ~Finish() { resolving=false; } } finish{resolving_};
    const auto generation=generation_;
    const bool scoped=HAILSTONE_HOST_HAS(host,get_player)&&HAILSTONE_HOST_HAS(host,server_info);
    const auto current=scoped ? scope(host) : std::optional<Scope>{};
    if (generation!=generation_) { reset(); return {}; }
    if (scoped && !current) { reset(); return {}; }
    if (scoped && current==scope_) return directory_;
    std::array<char,4096> path{};
    const auto n=host->character_config_dir(host->host,path.data(),path.size());
    if (n<=0 || static_cast<std::size_t>(n)>=path.size() || path[static_cast<std::size_t>(n)]!='\0' ||
        std::find(path.begin(),path.begin()+n,'\0')!=path.begin()+n) { reset(); return {}; }
    auto value=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()),static_cast<std::size_t>(n)));
    const auto after=scoped ? scope(host) : std::optional<Scope>{};
    if (generation!=generation_ || (scoped && current!=after)) { reset(); return {}; }
    scope_=after; directory_=std::move(value);
    return directory_;
}
Preferences::Preferences(std::string filename, std::span<const Preference> definitions)
    : filename_(std::move(filename)), definitions_(definitions), values_(defaults()) {}
std::map<std::string, std::int64_t> Preferences::defaults() const {
    std::map<std::string, std::int64_t> out;
    for (const auto& def : definitions_) out.emplace(def.key, def.initial);
    return out;
}
void Preferences::refresh(const PluginHostApi* host) {
    const auto current=directory_cache_.get(host);
    if (directory_ == current) return;
    directory_ = current; reload();
}
bool Preferences::reload() {
    values_ = defaults(); error_.clear();
    if (directory_.empty()) return true;
    std::ifstream input(directory_ / filename_, std::ios::binary);
    if (!input) { input.clear(); input.open(directory_ / (filename_ + ".previous"), std::ios::binary); }
    if (!input) return true;
    std::array<char, 16385> bytes{};
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto count = static_cast<std::size_t>(input.gcount());
    if (input.bad() || count > 16384) { error_ = "Settings could not be read; defaults are active."; return false; }
    std::string_view text(bytes.data(), count);
    auto replacement = defaults();
    unsigned lines = 0; bool malformed = false;
    while (!text.empty()) {
        const auto end = text.find('\n'); auto line = text.substr(0, end);
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        const auto equal = line.find('=');
        if (++lines > 256 || equal == line.npos || line.find('\0') != line.npos) { malformed = true; break; }
        const auto key = line.substr(0, equal), value = line.substr(equal + 1);
        const auto def = std::find_if(definitions_.begin(), definitions_.end(), [&](const auto& d) { return key == d.key; });
        if (def == definitions_.end()) continue;
        std::int64_t number = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || number < def->low || number > def->high) {
            error_ = "Settings contain an invalid value; defaults are active."; return false;
        }
        replacement[def->key] = number;
    }
    if (malformed || !text.empty()) { error_ = "Settings are malformed; defaults are active."; return false; }
    values_ = std::move(replacement); return true;
}
std::int64_t Preferences::get(std::string_view key) const {
    const auto it = values_.find(std::string(key)); return it == values_.end() ? 0 : it->second;
}
bool Preferences::save(const std::map<std::string, std::int64_t>& values) {
    if (directory_.empty()) { error_ = "Enter a character before saving these settings."; return false; }
    std::error_code ec; std::filesystem::create_directories(directory_, ec);
    if (ec) { error_ = "The settings directory is unavailable."; return false; }
    const auto temporary = directory_ / (filename_ + ".pending." + std::to_string(services::token()));
    const auto target = directory_ / filename_, backup = directory_ / (filename_ + ".previous");
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    for (const auto& [key, value] : values) file << key << '=' << value << '\n';
    file.flush(); file.close();
    if (!file) { std::filesystem::remove(temporary, ec); error_ = "Settings could not be saved."; return false; }
    const auto existing = std::filesystem::exists(target, ec);
    if (ec) { std::filesystem::remove(temporary, ec); error_ = "Previous settings are unavailable."; return false; }
    if (existing) {
        std::filesystem::remove(backup, ec);
        if (!ec) std::filesystem::rename(target, backup, ec);
        if (ec) { std::filesystem::remove(temporary, ec); error_ = "Previous settings could not be preserved."; return false; }
    }
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        std::error_code restore;
        if (existing) std::filesystem::rename(backup, target, restore);
        std::filesystem::remove(temporary, restore);
        error_ = "Settings could not be replaced; previous settings are preserved."; return false;
    }
    if (existing) std::filesystem::remove(backup, ec);
    values_ = values; error_.clear(); return true;
}
bool Preferences::set(std::string_view key, std::int64_t value) {
    const auto def = std::find_if(definitions_.begin(), definitions_.end(), [&](const auto& d) { return key == d.key; });
    if (def == definitions_.end() || value < def->low || value > def->high) { error_ = "Setting is outside its allowed range."; return false; }
    auto replacement = values_; replacement[def->key] = value; return save(replacement);
}
bool Preferences::reset() { return save(defaults()); }
}
