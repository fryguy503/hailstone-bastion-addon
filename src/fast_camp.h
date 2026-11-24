// SPDX-License-Identifier: MIT
#pragma once
#include "citadel_preferences.h"
#include "citadel_services.h"

namespace bastion::fast_camp {
inline constexpr std::uint16_t camp_opcode=0x28ec;
inline constexpr std::uint32_t accept_popup=2000000002u,decline_popup=2000000003u;
class Adapter {
public:
    Adapter();
    void set_host(const PluginHostApi*);
    void initialize();
    void shutdown();
    void peer(bool);
    void game_state(int);
    void begin_zone();
    void pulse();
    void outgoing(std::uint16_t);
    void observe_command(std::string_view);
    bool command(std::string_view);
    bool start(std::string_view destination={});
    bool set_enabled(bool);
    void cancel();
    bool supported() const noexcept;
    bool active() const { return selected_&&in_game_; }
    bool enabled() const { return settings_.get("enabled")==1; }
    bool armed() const { return armed_; }
    std::string_view message() const;
private:
    struct Scope {
        std::uint32_t player=0,zone=0,instance=0;
        std::uint64_t context=0,incarnation=0;
        std::string name;
        bool operator==(const Scope&) const=default;
    };
    struct Live { Scope scope;bool camping=false; };
    bool allowed(std::uint32_t) const noexcept;
    std::optional<Live> live() const;
    bool refresh_settings();
    void retire(std::string_view message={});
    bool arm(const Live&,bool explicit_request);
    static int callback(void*,const HailstoneArgs*,HailstoneOut*) noexcept;
    int call(const HailstoneArgs*,HailstoneOut*);
    const PluginHostApi* host_=nullptr;
    Preferences settings_;
    bool selected_=false,in_game_=false,armed_=false,starting_=false,observed_camp_=false,explicit_=false;
    std::optional<Scope> scope_;
    std::uint64_t generation_=services::token(),started_=0,deadline_=0,next_poll_=0;
    std::string message_="Use /fastcamp for one camp. Ordinary camping is unchanged by default.",error_;
};
} // namespace bastion::fast_camp
