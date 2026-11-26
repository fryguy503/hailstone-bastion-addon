// SPDX-License-Identifier: MIT
#pragma once

#include "citadel_services.h"
#include "nlohmann/json.hpp"

#include <array>

namespace bastion::journal {

inline constexpr std::uint16_t kOpcode = 0x7d27;

class Adapter {
public:
    void set_host(const PluginHostApi* host);
    void initialize();
    void peer(bool selected);
    void game_state(int state);
    void begin_zone();
    void pulse();
    bool packet(std::uint16_t opcode, const void* data, std::size_t size);
    bool open();

private:
    enum class Mode : std::uint16_t { Normal = 1, Bloodbound, Vigil, Copium, Sleepers, TofsGroup, TofsRaid, BrokenAccord, Firefell };
    static constexpr std::size_t kRelicDetailIndex = static_cast<std::size_t>(Mode::Firefell);
    struct Query {
        Mode mode;
        std::uint32_t relic = 0;
        bool operator==(const Query&) const = default;
    };
    struct Pending {
        Query query;
        std::uint32_t id;
        std::uint64_t sent_at;
    };
    struct Entry {
        std::uint32_t relic = 0;
        std::uint64_t received_at = 0;
        nlohmann::json data;
        bool unavailable = false;
    };

    static int callback(void* user, const HailstoneArgs* args, HailstoneOut* out) noexcept;
    int ui(const HailstoneArgs* args, HailstoneOut* out);
    std::optional<Query> query_from(const HailstoneArgs* args) const;
    static std::string_view mode_name(Mode mode);
    static std::size_t index(const Query& query);
    void close();
    void reset_session();
    void synchronize(bool force = false);
    void select(const Query& query, bool refresh);
    void unavailable(const Query& query);
    void send_query();
    void changed();
    void write_snapshot(const services::Output& result, const Query& query) const;
    bool can_inspect_item() const;
    bool inspect_item(std::uint32_t id);

    const PluginHostApi* host_ = nullptr;
    bool selected_ = false;
    bool in_game_ = false;
    bool shown_ = false;
    bool sent_ = false;
    std::uint64_t now_ = 0;
    std::uint64_t next_context_at_ = 0;
    std::uint64_t last_send_ = 0;
    std::uint64_t generation_ = 0;
    std::uint64_t revision_ = 0;
    std::optional<services::Context> context_;
    Query wanted_{Mode::Normal};
    std::optional<Query> queued_;
    std::optional<Pending> pending_;
    // One slot per wire mode, followed by a slot for the selected Copium relic.
    std::array<Entry, kRelicDetailIndex + 1> entries_{};
};

} // namespace bastion::journal
