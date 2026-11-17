// SPDX-License-Identifier: MIT
#pragma once
#include "citadel_services.h"
#include <optional>
#include <string>
namespace bastion::firefell {
inline constexpr std::uint16_t opcode=0x7d25;
const char* label(std::uint16_t version);
class Adapter {
public:
    void set_host(const PluginHostApi*);
    void initialize();
    void peer(bool);
    void game_state(int);
    void begin_zone();
    void pulse();
    bool packet(std::uint16_t,const void*,std::size_t);
    const char* name(std::uint32_t zone,std::uint32_t instance);
private:
    bool synchronize();
    void retire();
    static int callback(void*,const HailstoneArgs*,HailstoneOut*) noexcept;
    int ui(const HailstoneArgs*,HailstoneOut*);
    const PluginHostApi* host_=nullptr;
    bool selected_=false,in_game_=false,received_=false;
    std::optional<services::Context> context_;
    std::uint32_t instance_=0,nonce_=0;
    std::uint16_t version_=0;
    unsigned attempts_=0;
    std::uint64_t next_=0,last_=0;
};
}
