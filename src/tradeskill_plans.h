// SPDX-License-Identifier: MIT
#pragma once

#include "citadel_services.h"
#include "tradeskill_plans_protocol.h"
#include "nlohmann/json.hpp"
#include <filesystem>

namespace bastion::tradeskill_plans {
class Adapter {
  public:
    void set_host(const PluginHostApi *host);
    void initialize();
    void peer(bool enabled);
    void game_state(int state);
    void begin_zone();
    void pulse();
    bool packet(std::uint16_t opcode, const void *data, std::size_t size);
    bool open();

  private:
    using Json = nlohmann::json;
    enum class Request { List, Catalog, Plan, Preview, Write, Pinned };
    void reset_context();
    bool synchronize();
    bool send(Request request, const Json &body);
    bool busy() const { return pending_ != 0 || deferred_.has_value(); }
    bool write(const Json &body);
    bool retry();
    bool clear_write();
    std::filesystem::path outbox_path() const;
    bool queued() const;
    void receive(Request request, std::uint16_t status, Json body);
    void set_record(Json record, bool keep_selection = false);
    int call(const HailstoneArgs *args, HailstoneOut *out);
    static int callback(void *user, const HailstoneArgs *args, HailstoneOut *out) noexcept;
    void publish(const services::Output &out) const;
    bool update(const HailstoneArgs *args);

    const PluginHostApi *host_ = nullptr;
    std::optional<services::Context> context_;
    bool peer_ = false, in_game_ = false, shown_ = false, connected_ = false;
    bool catalog_more_ = false, retry_after_list_ = false, list_more_ = false;
    bool pin_checked_ = false, load_pinned_ = false;
    std::uint32_t pending_ = 0, list_page_ = 1;
    std::uint64_t sent_at_ = 0, revision_ = 0;
    Request request_ = Request::List;
    std::optional<std::pair<Request, Json>> deferred_;
    Reply reply_;
    std::string scope_, pinned_scope_, pinned_id_;
    std::string message_;
    Json plans_ = Json::array(), catalog_ = Json::array(), record_, preview_, pinned_record_;
};
} // namespace bastion::tradeskill_plans
