#pragma once
#include "citadel_preferences.h"
#include <array>
#include <functional>
#include <utility>

namespace bastion {
std::span<const Preference> citadel_preferences();
class CitadelHub {
public:
    CitadelHub();
    void set_host(const PluginHostApi* host);
    void initialize(std::function<bool(std::string_view)> open);
    void peer(bool selected);
    void game_state(int state);
    void begin_zone();
    void pulse();
    void open(std::string_view panel = "general");
    bool command(std::string_view arguments);
    std::uint32_t timestamp_style() const;
    bool right_click_sell() const;
    int map_spawn(const HailstoneSpawn*, HailstoneMapSpawn*);
    int map_policy(HailstoneMapPolicy*) const;
    int map_visible(const HailstoneSpawn*, std::uint32_t purpose) const;
    int merchant_right_click(const HailstoneMerchantSellContext*);
private:
    static int ui_callback(void*, const HailstoneArgs*, HailstoneOut*) noexcept;
    int ui(const HailstoneArgs*, HailstoneOut*);
    void report(std::string_view text);
    void refresh_settings();
    void observe_map_settings();
    bool set_preference(std::string_view key, std::int64_t value);
    float tracking_range() const;
    void retire_map_raid() const;
    bool map_raid_member(const HailstoneSpawn&, const HailstonePlayer&, const HailstoneSpawn&) const;
    struct MapRaid {
        std::uint64_t revision = 0, owner = 0;
        std::uint32_t player = 0, zone = 0, instance = 0, source_count = 0;
        std::array<std::pair<std::uint32_t,std::uint64_t>,72> actors{};
        std::size_t count = 0;
    };
    mutable MapRaid map_raid_;
    mutable std::uint64_t map_raid_generation_ = 0;
    mutable bool map_raid_running_ = false;
    const PluginHostApi* host_ = nullptr;
    bool selected_ = false, in_game_ = false, shown_ = false;
    std::uint64_t open_revision_ = 0;
    std::uint64_t map_revision_ = 0;
    std::array<std::int64_t,6> map_values_{1,1,1,1,1,1};
    std::string panel_ = "general", message_;
    std::function<bool(std::string_view)> open_;
    Preferences settings_{"citadel.cfg", citadel_preferences()};
};
}
