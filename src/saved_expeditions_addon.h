#pragma once
#include "citadel_services.h"
#include "saved_expeditions_protocol.h"
namespace bastion::saved_expeditions {
class Adapter {
  public:
    void set_host(const PluginHostApi *);
    void initialize();
    void shutdown();
    void peer(bool);
    void game_state(int);
    void begin_zone();
    void pulse();
    bool packet(std::uint16_t, const void *, std::size_t);
    bool command(std::string_view);
    bool refresh();
    bool resume(std::uint64_t revision, std::uint32_t dz, std::string_view uuid);
    bool fresh() const;
    bool pending() const { return pending_ != 0; }
    std::uint64_t revision() const { return revision_; }
    const std::vector<Entry> &entries() const { return entries_; }

  private:
    bool synchronize();
    void retire();
    bool send(Action, const Entry * = nullptr);
    std::uint64_t remaining(const Entry &) const;
    static int ui(void *, const HailstoneArgs *, HailstoneOut *) noexcept;
    int call(const HailstoneArgs *, HailstoneOut *);
    const PluginHostApi *host_ = nullptr;
    bool selected_ = false, in_game_ = false, shown_ = false;
    std::optional<services::Context> context_;
    std::uint64_t revision_ = services::token(), now_ = 0, sent_at_ = 0, received_at_ = 0, server_time_ = 0;
    bool sent_ = false, usable_ = false;
    std::uint32_t pending_ = 0;
    Action action_ = Action::List;
    std::vector<Entry> entries_;
    std::string message_ = "Refresh to load saved expeditions.";
};
} // namespace bastion::saved_expeditions
