// SPDX-License-Identifier: MIT
#pragma once

#include "citadel_services.h"
#include "raid_ready_check_protocol.h"

namespace bastion::ready_check {

class Adapter {
public:
    void set_host(const PluginHostApi*);
    void initialize();
    void peer(bool);
    void game_state(int);
    void begin_zone();
    void pulse();
    bool packet(uint16_t opcode, const void* data, std::size_t size);

private:
    struct Context {
        uint32_t spawn_id;
        uint32_t zone_id;
        uint32_t instance_id;
        std::string name;
        bool in_raid;
        bool operator==(const Context&) const = default;
    };

    void reset();
    void observe(bool force = false);
    bool fresh() const;
    uint32_t remaining_ms() const;
    bool send(Action, uint64_t check_id = 0, Status choice = Status::NoResponse) const;
    std::string_view request(std::string_view operation, const HailstoneArgs*);
    static int callback(void*, const HailstoneArgs*, HailstoneOut*) noexcept;
    int ui(const HailstoneArgs*, HailstoneOut*);

    const PluginHostApi* host_ = nullptr;
    bool selected_ = false, in_game_ = false;
    std::optional<Context> context_;
    std::optional<Snapshot> snapshot_;
    uint64_t now_ = 0, received_at_ = 0;
    uint64_t next_observation_ = 0, next_hello_ = 0, next_action_ = 0;
};

}  // namespace bastion::ready_check
