#pragma once
#include "citadel_services.h"
#include "home_city_protocol.h"
#include <set>
namespace bastion::home_city {
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
    bool refresh(bool probe = false);
    bool choose(std::uint64_t revision, std::uint32_t choice, std::uint64_t grant, std::uint64_t snapshot);
    bool suppress(std::uint64_t revision, std::uint64_t grant, std::uint64_t snapshot);
    bool fresh() const;
    bool pending() const { return pending_ != 0; }
    std::uint64_t revision() const { return revision_; }
    const Response &state() const { return state_; }
    bool shown() const { return shown_; }
    std::uint32_t preselection() const { return preselected_choice_; }
    const std::string &message() const { return message_; }

  private:
    void retire();
    bool synchronize();
    bool send(Action, std::uint32_t = 0);
    void apply_requested_choice();
    void report(std::string);
    static int ui(void *, const HailstoneArgs *, HailstoneOut *) noexcept;
    int call(const HailstoneArgs *, HailstoneOut *);
    const PluginHostApi *host_ = nullptr;
    bool selected_ = false, in_game_ = false, shown_ = false, sent_ = false, usable_ = false;
    std::optional<services::Context> context_;
    std::uint64_t revision_ = services::token(), now_ = 0, sent_at_ = 0, received_at_ = 0;
    std::uint32_t pending_ = 0, expected_home_ = 0;
    std::uint32_t requested_choice_ = 0, preselected_choice_ = 0;
    std::uint64_t selection_revision_ = services::token();
    Action action_ = Action::List;
    std::uint64_t expected_grant_ = 0;
    Response state_;
    std::string message_ = "Refresh to load available home city choices.";
    std::optional<std::uint64_t> safe_since_, next_probe_;
    std::set<std::uint64_t> shown_grants_;
    std::string reminder_owner_;
};
} // namespace bastion::home_city
