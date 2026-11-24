#pragma once

#include "hailstone_plugin.h"
#include "raid_markers_protocol.h"

#include <array>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace bastion::raid_markers {

class Adapter {
public:
    void set_host(const PluginHostApi* host);
    void initialize();
    void peer(bool selected);
    void game_state(int state);
    void world_login(bool zoning);
    void begin_zone();
    void pulse();
    void world_frame(const HailstoneWorldFrame* frame) const;
    bool packet(std::uint16_t opcode, const void* data, std::size_t size);

private:
    bool allowed() const;
    bool refresh_roster();
    void reset_session();
    void reset_raid();
    void cancel_aim();
    bool has_marker_role() const;
    bool can_place() const;
    const HS_BulkRaidData* member(std::string_view name) const;
    void observe_raid(std::span<const std::byte> bytes);
    bool manager_list(std::span<const std::byte> bytes);
    void query_managers(std::uint64_t now);
    void receive(const Received& incoming, std::uint64_t now);
    void apply(const Message& message);
    bool enqueue(Message message);
    bool send(const Message& message, std::string_view recipient, std::uint64_t now);
    void sync(std::uint64_t now);
    int ui(const HailstoneArgs* args, HailstoneOut* out);
    static int callback(void* user, const HailstoneArgs* args, HailstoneOut* out) noexcept;

    const PluginHostApi* host_ = nullptr;
    bool selected_ = false;
    bool in_zone_ = false;
    bool world_zoning_ = false;
    bool hosting_ = false;
    bool dirty_ = false;
    bool check_leader_ = false;
    bool query_pending_ = false;
    std::string player_;
    std::string effective_leader_;
    std::string actual_leader_;
    std::vector<HS_BulkRaidData> roster_;
    std::array<std::optional<Marker>, kMarkerCount> slots_{};
    std::deque<Received> manager_requests_;
    std::deque<Message> outgoing_;
    std::uint64_t nonce_ = 0;
    std::uint64_t revision_ = 0;
    std::uint64_t retired_nonce_ = 0;
    std::uint64_t next_send_ = 0;
    std::uint64_t next_query_ = 0;
    std::uint64_t query_until_ = 0;
    std::uint64_t last_snapshot_ = 0;
    std::uint64_t roster_rebuild_until_ = 0;
    bool sync_needed_ = false;
    std::uint8_t aiming_ = 0;
    HailstoneWorldPlacement placement_{};
    std::string error_;
};

} // namespace bastion::raid_markers
