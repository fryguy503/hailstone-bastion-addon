#include "gear_score_addon.h"
#include "citadel_services.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
#include <iterator>
#include <string_view>

namespace bastion::gear_score {
namespace {
std::uint32_t next_session() {
    // A plugin's process-lifetime sequence does not reuse a nonce when an
    // adapter is reconstructed or reconnects to the same numeric zone/player.
    static std::atomic<std::uint32_t> sequence{0};
    auto value = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!value) value = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    return value;
}
template<std::size_t N> std::string bounded(const char (&text)[N]) {
    return {text, std::find(text, text + N, '\0')};
}
} // namespace

void Addon::retire() {
    state_.retire(); attempts_ = 0; next_hello_.reset(); last_tick_.reset();
    zone_ = player_ = 0; character_.clear(); description_.clear(); label_.clear();
}
void Addon::set_host(const PluginHostApi* host) {
    if (HAILSTONE_HOST_HAS(host_, remove_ui_function)) host_->remove_ui_function(host_->host, "gear_score");
    retire(); selected_ = in_game_ = false; host_ = host;
}
void Addon::initialize() {
    if (HAILSTONE_HOST_HAS(host_, add_ui_function)) host_->add_ui_function(host_->host, "gear_score", ui_callback, this);
}
void Addon::peer(bool selected) { selected_ = selected; retire(); }
void Addon::game_state(int value) {
    in_game_ = value == GAMESTATE_INGAME;
    if (!in_game_) retire();
}
void Addon::begin_zone() { retire(); in_game_ = false; }
bool Addon::player(HailstonePlayer& out) const {
    out = {}; out.size = sizeof(out);
    return selected_ && in_game_ && HAILSTONE_HOST_HAS(host_, get_player) && host_->get_player(host_->host, &out) &&
        out.size >= offsetof(HailstonePlayer, in_zone) + sizeof(out.in_zone) && out.in_zone && out.spawn_id;
}
void Addon::pulse() {
    HailstonePlayer current{};
    if (!player(current)) { if (state_.session()) retire(); return; }
    if (!HAILSTONE_HOST_HAS(host_, monotonic_ms)) return;
    const auto now = host_->monotonic_ms(host_->host);
    const auto name = bounded(current.name);
    if (!state_.session() || zone_ != current.zone_id || player_ != current.spawn_id || character_ != name) {
        retire(); state_.begin(next_session());
        zone_ = current.zone_id; player_ = current.spawn_id; character_ = name;
    }
    if (last_tick_ && now < *last_tick_) next_hello_.reset();
    last_tick_ = now;
    if (state_.ready() || attempts_ == 5 || (next_hello_ && now < *next_hello_) ||
        !HAILSTONE_HOST_HAS(host_, send_packet)) return;
    const auto session = state_.session();
    const auto bytes = hello(session);
    next_hello_ = now > std::numeric_limits<std::uint64_t>::max() - 2000 ?
        std::numeric_limits<std::uint64_t>::max() : now + 2000;
    ++attempts_;
    const bool sent = host_->send_packet(host_->host, HAILSTONE_STREAM_ZONE, kOpcode, bytes.data(), bytes.size()) != 0;
    // A test/host can synchronously deliver an ACK or retire the adapter while
    // sending. Never restore counters belonging to a different session.
    if (!sent && state_.session() == session && attempts_) --attempts_;
}
bool Addon::retry() {
    HailstonePlayer current{};
    if (!player(current) || state_.ready() || !HAILSTONE_HOST_HAS(host_, monotonic_ms) ||
        !HAILSTONE_HOST_HAS(host_, send_packet)) return false;
    attempts_ = 0; next_hello_.reset(); return true;
}
bool Addon::packet(std::uint16_t opcode, const void* bytes, std::size_t size) {
    if (!selected_ || opcode != kOpcode) return false;
    if ((!bytes && size) || !in_game_) return true;
    const auto message = decode({static_cast<const std::byte*>(bytes), size});
    if (message) state_.receive(*message);
    return true;
}
const char* Addon::item_description_append(const HailstoneItem* item) {
    if (!selected_ || !in_game_ || !item || item->size < offsetof(HailstoneItem, id) + sizeof(item->id) || !item->id)
        return nullptr;
    Key key; key.item = item->id;
    bool complete = false;
    if (item->size >= offsetof(HailstoneItem, configuration_complete) + sizeof(item->configuration_complete)) {
        std::copy(std::begin(item->augment_ids), std::end(item->augment_ids), key.augments.begin());
        complete = item->configuration_complete == 1;
    }
    description_ = state_.description(key, complete);
    return description_.empty() ? nullptr : description_.c_str();
}
const char* Addon::label(int id) {
    if (!selected_ || !in_game_ || (id != 1801 && id != 1802)) return nullptr;
    label_ = id == 1801 ? state_.equipped() : state_.owned();
    return label_.c_str();
}
int Addon::ui_callback(void* self, const HailstoneArgs* args, HailstoneOut* out) noexcept {
    if (!self) return 0;
    auto &owner = *static_cast<Addon*>(self);
    return services::guarded_ui(owner.host_, [&] { return owner.ui(args,out); });
}
int Addon::ui(const HailstoneArgs* args, HailstoneOut* out) {
    if (!out || !HAILSTONE_HOST_HAS(host_, arg) || !HAILSTONE_HOST_HAS(host_, out_begin_table) ||
        !HAILSTONE_HOST_HAS(host_, out_key) || !HAILSTONE_HOST_HAS(host_, out_value) || !HAILSTONE_HOST_HAS(host_, out_end)) return 0;
    HailstoneValue operation{};
    if (!host_->arg(host_->host, args, 0, &operation) || operation.kind != HAILSTONE_STRING || !operation.s) return 0;
    std::size_t length = 0;
    while (length < 16 && operation.s[length]) ++length;
    if (length == 16) return 0;
    const std::string_view name(operation.s, length);
    if (name != "state" && name != "retry") return 0;
    const bool retried = name == "retry" && retry();
    host_->out_begin_table(host_->host, out);
    const auto number = [&](const char* key, std::int64_t value) {
        host_->out_key(host_->host, out, key); HailstoneValue v{}; v.kind = HAILSTONE_INT; v.i = value;
        host_->out_value(host_->host, out, &v);
    };
    const auto text = [&](const char* key, const std::string& value) {
        host_->out_key(host_->host, out, key); HailstoneValue v{}; v.kind = HAILSTONE_STRING; v.s = value.c_str();
        host_->out_value(host_->host, out, &v);
    };
    number("active", selected_ && in_game_ ? 1 : 0);
    number("ready", state_.ready() ? 1 : 0);
    number("summary_received", state_.summary().received ? 1 : 0);
    number("attempts", attempts_);
    HailstonePlayer current{};
    const bool can_retry = !state_.ready() && HAILSTONE_HOST_HAS(host_, monotonic_ms) &&
        HAILSTONE_HOST_HAS(host_, send_packet) && player(current);
    number("can_retry", can_retry ? 1 : 0);
    number("retried", retried ? 1 : 0);
    number("templates", static_cast<std::int64_t>(state_.templates()));
    number("configurations", static_cast<std::int64_t>(state_.configurations()));
    text("equipped", state_.equipped()); text("owned", state_.owned());
    host_->out_end(host_->host, out);
    return 1;
}
} // namespace bastion::gear_score
