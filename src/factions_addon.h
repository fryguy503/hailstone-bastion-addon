#pragma once
#include "citadel_services.h"
#include "factions_protocol.h"
namespace bastion::factions {
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
    bool refresh(bool force = false);
    bool export_file(std::string_view filename = {});
    bool fresh() const;
    bool pending() const { return request_ != 0; }
    bool shown() const { return shown_; }
    std::string_view search() const { return search_; }
    std::uint64_t revision() const { return revision_; }
    const std::vector<Category> &categories() const { return categories_; }
    const std::vector<Entry> &entries() const { return entries_; }

  private:
    bool synchronize();
    void retire();
    void failure(std::string, bool retry);
    void publish();
    static int ui(void *, const HailstoneArgs *, HailstoneOut *) noexcept;
    int call(const HailstoneArgs *, HailstoneOut *);
    const PluginHostApi *host_ = nullptr;
    bool selected_ = false, in_game_ = false, shown_ = false, sent_ = false, usable_ = false, assembly_ = false;
    std::optional<services::Context> context_;
    std::uint64_t revision_ = services::token(), now_ = 0, sent_at_ = 0, progress_at_ = 0, received_at_ = 0, hash_ = 0,
                  requested_hash_ = 0;
    std::optional<std::uint64_t> retry_at_;
    unsigned failures_ = 0;
    std::uint32_t request_ = 0, session_ = 0, character_ = 0;
    Packet header_;
    std::vector<Category> categories_, incoming_categories_;
    std::vector<Entry> entries_, incoming_entries_;
    std::vector<bool> category_seen_, entry_seen_;
    std::string message_ = "Open Factions to load received standings.", search_;
    std::uint64_t search_revision_ = services::token();
};
} // namespace bastion::factions
