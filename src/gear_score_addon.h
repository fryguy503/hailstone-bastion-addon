#pragma once

#include "gear_score_state.h"
#include "hailstone_plugin.h"
#include <optional>
#include <string>

namespace bastion::gear_score {
class Addon {
public:
    void set_host(const PluginHostApi* host);
    void initialize();
    void peer(bool selected);
    void game_state(int value);
    void begin_zone();
    void pulse();
    // True consumes this selected server's private opcode, including invalid packets.
    bool packet(std::uint16_t opcode, const void* bytes, std::size_t size);
    const char* item_description_append(const HailstoneItem* item);
    const char* label(int id); // 1801 Equipped, 1802 Best Owned; all other ids preserve stock
    std::uint64_t revision() const { return state_.revision(); }
    bool retry(); // explicit UI negotiation retry, never a game action
    const State& state() const { return state_; }
    unsigned attempts() const { return attempts_; }
private:
    void retire();
    bool player(HailstonePlayer& out) const;
    static int ui_callback(void* self, const HailstoneArgs* args, HailstoneOut* out) noexcept;
    int ui(const HailstoneArgs* args, HailstoneOut* out);
    const PluginHostApi* host_ = nullptr;
    bool selected_ = false, in_game_ = false;
    State state_;
    std::uint32_t zone_ = 0, player_ = 0;
    std::string character_, description_, label_;
    unsigned attempts_ = 0;
    std::optional<std::uint64_t> next_hello_, last_tick_;
};
} // namespace bastion::gear_score
