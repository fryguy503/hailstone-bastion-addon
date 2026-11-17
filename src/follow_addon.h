#pragma once

#include "hailstone_plugin.h"
#include "hailstone_follow.h"
#include "citadel_preferences.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace bastion::follow {
struct Settings {
    bool enabled = false, cancel_on_move = true, instant_mount = true;
    int distance_hundredths = 1500;
    bool operator==(const Settings&) const = default;
    std::string serialize() const;
    static std::optional<Settings> parse(std::string_view text);
};
std::optional<int> parse_distance(std::string_view text);
HailstoneFollowProfile profile(const Settings& settings);

class Addon {
public:
    void set_host(const PluginHostApi* host);
    void initialize();
    void peer(bool selected);
    void game_state(int value);
    void begin_zone();
    void pulse();
    int follow_profile(HailstoneFollowProfile* out) const;
    int mount_profile(HailstoneMountProfile* out) const;
    // Full manually entered line. Returns true only for this module's aliases.
    bool command(std::string_view line);
    const Settings& settings() const { return settings_; }
private:
    bool ready() const;
    void refresh_settings();
    bool change(std::string_view key, double value);
    bool save(const Settings& replacement);
    bool control(const char* action);
    void message(std::string_view text) const;
    static int ui_callback(void*, const HailstoneArgs*, HailstoneOut*) noexcept;
    int ui(const HailstoneArgs*, HailstoneOut*);
    const PluginHostApi* host_ = nullptr;
    bool selected_ = false;
    Settings settings_;
    std::filesystem::path directory_;
    CharacterDirectory directory_cache_;
    std::string error_;
};
} // namespace bastion::follow
