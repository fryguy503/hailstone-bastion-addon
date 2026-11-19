// SPDX-License-Identifier: MIT
#pragma once
#include "citadel_preferences.h"
#include "hailstone_plugin.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bastion::twist {
struct Settings {
    int delay=33,adjust=1,recast=0;
    bool quiet=false,allow_nonbard=false;
};
// Citadel's scheduler uses these historical duration formulas, rather than the
// game's focused buff duration. Inputs are public spell-record observations.
std::optional<std::uint64_t> duration_ticks(int formula,std::uint32_t cap,std::uint32_t level);
class Adapter {
public:
    Adapter();
    void set_host(const PluginHostApi*);
    void initialize();
    void peer(bool);
    void world_login(bool zoning);
    void game_state(int);
    void begin_zone();
    void pulse();
    bool command(std::string_view full_line);
    void observe_command(std::string_view full_line);
    int incoming_chat(std::string_view);
    const Settings& settings() const {return settings_;}
    bool running() const {return running_;}
    int current() const {return running_?(hold_?hold_:current_):0;}
    int next() const;
    std::string queue() const;
    const std::string& error() const {return error_;}
private:
    struct Identity {
        std::uint32_t player=0,zone=0,instance=0;
        std::uint64_t incarnation=0,context=0;
        std::string character,server;
        bool operator==(const Identity&) const=default;
    };
    struct Observation {Identity identity;HailstonePlayer player{};};
    struct Rotation {
        std::vector<int> gems;
        std::vector<std::uint64_t> due;
        std::size_t cursor=0,last=0;
        bool completed=false;
    };
    struct Owned {Identity identity;std::uint32_t spell=0;};
    std::optional<Observation> observe(bool actions) const;
    bool authorized(const HailstonePlayer&) const;
    bool can(std::uint32_t) const;
    std::optional<std::string> parse(std::string_view) const;
    std::optional<bool> flag(std::string_view) const;
    std::optional<std::uint64_t> number(std::string_view) const;
    void registrations();
    void refresh_settings();
    bool setting(std::string_view,int);
    void retire(bool clear_queue);
    void pause(std::string_view);
    void message(std::string_view);
    void stop_owned();
    bool dispatch(std::string_view);
    void tick();
    bool execute(std::string_view);
    static int ui_callback(void*,const HailstoneArgs*,HailstoneOut*) noexcept;
    int ui(const HailstoneArgs*,HailstoneOut*);
    static int tlo_callback(void*,const char*,const char*,const char*,HailstoneValue*) noexcept;
    int tlo(const char*,HailstoneValue*);
    const PluginHostApi* host_=nullptr;
    Preferences preferences_;
    Settings settings_;
    Rotation rotation_;
    std::optional<Rotation> saved_;
    std::optional<Identity> identity_;
    std::optional<Owned> owned_;
    std::optional<std::pair<std::string,std::string>> retained_profile_;
    bool selected_=false,in_game_=false,running_=false,busy_=false,dispatching_=false,tlo_added_=false,debug_=false,one_shot_=false,zoning_=false;
    int hold_=0,current_=0;
    std::uint64_t generation_=0,next_poll_=0,next_action_=0,last_clock_=0;
    bool clock_seen_=false;
    std::string error_,message_,tlo_text_;
};
} // namespace bastion::twist
