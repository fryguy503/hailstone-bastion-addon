// SPDX-License-Identifier: MIT
#pragma once

#include "citadel_services.h"

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
    enum class Mode : std::uint16_t { Normal = 1, Bloodbound, Vigil, Copium };
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

    static int callback(void* user, const HailstoneArgs* args, HailstoneOut* out) noexcept;
    int ui(const HailstoneArgs* args, HailstoneOut* out);
    static std::optional<Query> parse_query(std::string_view message);
    static std::string_view mode_name(Mode mode);
    void browse(bool backup = false);
    void close_tab();
    void reset_session();
    void synchronize();
    bool trusted_page() const;
    std::string_view page_url() const;
    bool notify(std::string_view payload) const;
    void unavailable(const Query& query) const;
    void read_page_messages();
    void send_query();

    const PluginHostApi* host_ = nullptr;
    bool selected_ = false;
    bool in_game_ = false;
    bool shown_ = false;
    bool ready_ = false;
    bool opening_ = false;
    bool backup_ = false;
    bool bundle_sent_ = false;
    bool using_bundle_ = false;
    bool sent_ = false;
    std::uint32_t tab_ = 0;
    std::uint64_t opened_at_ = 0;
    std::uint64_t last_send_ = 0;
    std::optional<std::uint64_t> last_library_open_;
    std::uint64_t now_ = 0;
    std::optional<services::Context> context_;
    std::optional<Query> queued_;
    std::optional<Pending> pending_;
    std::string reply_;
};

} // namespace bastion::journal
