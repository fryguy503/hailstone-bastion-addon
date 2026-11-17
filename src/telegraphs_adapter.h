#pragma once

#include "hailstone_plugin.h"
#include "telegraphs_geometry.h"
#include "citadel_preferences.h"

#include <deque>
#include <map>
#include <optional>

namespace bastion::telegraphs {
std::span<const Preference> preferences();

// Main-thread SDK-only bridge. Frame availability comes exclusively from the
// live host callback/services; compiled support or settings alone never qualify.
class Adapter final : private WireSender {
public:
    void set_host(const PluginHostApi* host);
    void initialize();
    bool command(std::string_view arguments);
    void peer(bool selected); // Every new world announcement/login resets this context.
    void game_state(int state);
    void begin_zone();
    void pulse();
    void receive(const void* bytes, std::size_t size);
    void world_frame(const HailstoneWorldFrame* frame);
    void world_result(const HailstoneWorldReceipt* receipt);
private:
    struct Accepted {
        MarkerReceipt marker;
        std::uint64_t revision = 0;
        std::uint32_t vertex_count = 0;
        bool recorded = false, received = false;
    };
    struct PendingFrame {
        std::uint64_t host_frame = 0, session_frame = 0;
        std::uint32_t epoch = 0;
        std::map<std::uint64_t, Accepted> batches;
    };
    bool services() const;
    bool clock(std::uint64_t& now);
    void unavailable();
    bool send(const Bytes& bytes) override;
    void draw(const HailstoneWorldFrame& host_frame, std::uint32_t now);
    void draw_local(const HailstoneWorldFrame&, std::uint32_t now, GeometryQueries&, GeometryBudget&);
    GeometryOptions geometry_options() const;
    bool test(std::string_view arguments);
    static int ui_callback(void*, const HailstoneArgs*, HailstoneOut*) noexcept;
    int ui(const HailstoneArgs*, HailstoneOut*);
    void report(std::string_view text);

    const PluginHostApi* host_ = nullptr;
    Session session_;
    bool selected_ = false, in_zone_ = false, available_ = false;
    std::uint32_t epoch_ = 0;
    std::optional<std::uint32_t> retired_zone_epoch_;
    std::optional<std::uint32_t> context_retired_epoch_;
    std::optional<std::uint64_t> last_clock_, last_frame_clock_;
    std::uint64_t last_host_frame_ = 0, next_tag_ = 0;
    std::size_t required_cursor_ = 0, optional_cursor_ = 0;
    std::deque<PendingFrame> pending_; // Matches Session's eight issued-frame bound.
    Preferences preferences_{"telegraphs.cfg", preferences()};
    std::vector<RenderMarker> local_;
    std::uint64_t local_serial_ = 0;
    std::uint32_t local_submitted_ = 0;
    std::uint32_t local_player_ = 0, local_zone_ = 0;
    std::uint64_t local_owner_ = 0;
    std::string message_;
};
} // namespace bastion::telegraphs
