#pragma once

#include "hailstone_plugin.h"
#include "citadel_preferences.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bastion::raidbars {
struct Setting { const char* key; int minimum, maximum, initial; };
const std::vector<Setting>& settings_definitions();
struct Settings {
    Settings();
    std::map<std::string, int, std::less<>> values;
    std::vector<std::uint32_t> priority;
    int get(std::string_view key) const;
    bool set(std::string_view key, int value);
    bool set_priority(std::string_view names);
    std::string priority_text() const;
};
struct Vital { bool known = false; std::uint32_t percent = 0; };
struct Row {
    std::string name;
    std::uint32_t group = 0, class_id = 0, level = 0, flags = 0, spawn_id = 0, owner_id = 0;
    std::uint64_t incarnation = 0;
    bool pet = false, self = false;
    Vital health, mana, endurance;
    std::vector<Row> pets;
};
// Unknown health never becomes a synthetic zero or satisfies a low-HP filter.
std::vector<Row> visible_rows(std::vector<Row> rows, const Settings& settings);

class Adapter {
public:
    void set_host(const PluginHostApi* host);
    void initialize();
    void shutdown();
    void peer(bool selected);
    void game_state(int state);
    void begin_zone();
    void pulse() {}
    bool command(std::string_view args);
private:
    struct RosterState {
        std::uint64_t revision = 0, owner_incarnation = 0;
        std::size_t count = 0;
    };
    bool sample(RosterState& roster, std::vector<Row>& rows);
    void clear_roster();
    void load_settings();
    bool save_settings();
    bool select(const std::vector<Row>& rows, std::uint32_t, std::uint64_t);
    static int callback(void*, const HailstoneArgs*, HailstoneOut*) noexcept;
    static int stock_callback(void*, const HailstoneArgs*, HailstoneOut*) noexcept;
    int ui(const HailstoneArgs*, HailstoneOut*);
    int stock_action(const HailstoneArgs*, HailstoneOut*);
    const PluginHostApi* host_ = nullptr;
    Settings settings_;
    std::filesystem::path file_;
    CharacterDirectory directory_cache_;
    std::string identity_, error_;
    struct ManagerReview {
        std::uint64_t revision = 0, owner_incarnation = 0;
        std::string action, name, token;
    };
    std::optional<ManagerReview> manager_review_, stock_manager_review_;
    void retire_manager_reviews(const RosterState&, bool available);
    bool manager_request(const RosterState&, bool available, std::optional<std::uint64_t> revision,
                         std::string_view verb, std::string_view target, bool stock,
                         std::string_view review_token, std::string& error);
    struct ManagerMember {
        std::string name;
        bool self;
        std::uint32_t group, class_id, level, flags, spawn_id;
        std::uint64_t incarnation;
        bool operator==(const ManagerMember&) const = default;
    };
    RosterState roster_state_;
    bool roster_locked_ = false;
    std::vector<ManagerMember> roster_members_;
    std::array<std::uint32_t,13> group_counts_{};
    bool selected_ = false, in_zone_ = false, initialized_ = false, local_leader_ = false;
    bool stock_function_ = false;
};
} // namespace bastion::raidbars
