#include "saved_expeditions_addon.h"
namespace bastion::saved_expeditions {
void Adapter::retire() {
    context_.reset();
    pending_ = 0;
    entries_.clear();
    usable_ = false;
    shown_ = false;
    sent_ = false;
    revision_ = services::token();
    message_ = "Refresh to load saved expeditions.";
}
void Adapter::set_host(const PluginHostApi *h) {
    shutdown();
    host_ = h;
}
void Adapter::initialize() {
    if (HAILSTONE_HOST_HAS(host_, add_ui_function))
        host_->add_ui_function(host_->host, "saved_expeditions", ui, this);
}
void Adapter::shutdown() {
    if (HAILSTONE_HOST_HAS(host_, remove_ui_function))
        host_->remove_ui_function(host_->host, "saved_expeditions");
    retire();
    selected_ = in_game_ = false;
}
void Adapter::peer(bool value) {
    selected_ = value;
    retire();
}
void Adapter::game_state(int value) {
    in_game_ = value == GAMESTATE_INGAME;
    if (!in_game_)
        retire();
}
void Adapter::begin_zone() {
    retire();
    in_game_ = false;
}
bool Adapter::synchronize() {
    const auto current = services::context(host_, selected_, in_game_);
    if (current != context_) {
        retire();
        context_ = current;
    }
    if (!current)
        return false;
    const auto time = host_->monotonic_ms(host_->host);
    if (time < now_) {
        retire();
        context_ = current;
    }
    now_ = time;
    return true;
}
void Adapter::pulse() {
    if (!synchronize())
        return;
    if (pending_ && now_ - sent_at_ >= 10000) {
        pending_ = 0;
        usable_ = false;
        message_ = action_ == Action::Resume
                       ? "No rejoin reply received. Refresh to check membership; no automatic retry was sent."
                       : "No reply received. Refresh when the server is ready.";
    }
    if (usable_ && now_ - received_at_ >= 60000) {
        usable_ = false;
        revision_ = services::token();
        message_ = "Saved list expired. Refresh before rejoining.";
    }
}
bool Adapter::fresh() const { return usable_ && !pending_ && now_ >= received_at_ && now_ - received_at_ < 60000; }
std::uint64_t Adapter::remaining(const Entry &e) const {
    if (now_ < received_at_)
        return 0;
    const auto elapsed = (now_ - received_at_) / 1000;
    if (elapsed > UINT64_MAX - server_time_)
        return 0;
    const auto time = server_time_ + elapsed;
    return e.expires > time ? e.expires - time : 0;
}
bool Adapter::send(Action action, const Entry *entry) {
    if (!synchronize() || pending_ || (sent_ && now_ - sent_at_ < 1100))
        return false;
    const auto id = services::request_id();
    const auto bytes = request(action, id, entry);
    if (bytes.empty())
        return false;
    pending_ = id;
    action_ = action;
    sent_ = true;
    sent_at_ = now_;
    usable_ = false;
    message_ =
        action == Action::List ? "Loading saved expeditions..." : "Rejoin request sent; waiting for the server...";
    const auto context = context_;
    const bool admitted =
        host_->send_packet(host_->host, HAILSTONE_STREAM_ZONE, kOpcode, bytes.data(), bytes.size()) != 0;
    if (!admitted && context_ == context && pending_ == id) {
        pending_ = 0;
        message_ = "Connection unavailable. Refresh before trying again.";
    }
    return admitted;
}
bool Adapter::refresh() { return send(Action::List); }
bool Adapter::resume(std::uint64_t revision, std::uint32_t dz, std::string_view uuid) {
    pulse();
    if (revision != revision_ || !fresh())
        return false;
    for (const auto &e : entries_)
        if (e.dz_id == dz && e.uuid == uuid && e.eligibility == Result::Ok && remaining(e)) {
            const auto copy = e;
            return send(Action::Resume, &copy);
        }
    return false;
}
bool Adapter::packet(std::uint16_t opcode, const void *data, std::size_t size) {
    if (!selected_ || opcode != kOpcode)
        return false;
    pulse();
    if (!context_ || !pending_ || (!data && size))
        return true;
    const auto value = decode({static_cast<const std::byte *>(data), size});
    if (!value || value->request != pending_ ||
        value->action != (action_ == Action::List ? Action::ListResult : Action::ResumeResult) ||
        (value->action == Action::ListResult && value->result == Result::Ok && !value->server_time))
        return true;
    pending_ = 0;
    usable_ = false;
    revision_ = services::token();
    message_ = value->message.empty() ? result_text(value->result) : value->message;
    if (value->action == Action::ListResult) {
        entries_ = value->entries;
        server_time_ = value->server_time;
        received_at_ = now_;
        usable_ = value->result == Result::Ok;
        if (usable_ && entries_.empty())
            message_ = "No unexpired saved expeditions were returned.";
    } else if (value->result == Result::Ok)
        message_ = "Rejoined. Use the ordinary NPC or entrance to enter; this does not teleport you.";
    return true;
}
bool Adapter::command(std::string_view text) {
    if (!synchronize())
        return false;
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    if (text.size() > 512 || std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        return false;
    std::string action(text);
    for (char &c : action) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    if (!action.empty() && action != "open" && action != "ui" && action != "refresh" && action != "help") {
        if (!HAILSTONE_HOST_HAS(host_, do_command)) return false;
        // Preserve the owned DLL's server shortcuts, including delegate list,
        // saved and resume arguments. Names and UUIDs keep their original case.
        const std::string command = "/say #dzmanager " + std::string(text);
        host_->do_command(host_->host, command.c_str());
        return true;
    }
    shown_ = true;
    if (action == "refresh" || (!pending_ && !fresh()))
        refresh();
    return true;
}
int Adapter::ui(void *self, const HailstoneArgs *args, HailstoneOut *out) noexcept {
    if (!self) return 0;
    auto &owner = *static_cast<Adapter*>(self);
    return services::guarded_ui(owner.host_, [&] { return owner.call(args,out); });
}
int Adapter::call(const HailstoneArgs *args, HailstoneOut *out) {
    const auto operation = services::argument(host_, args, 0, 16);
    if (!operation)
        return 0;
    if (*operation == "visibility") {
        services::Output o{host_, out};
        if (!o.valid()) return 0;
        const bool active = selected_ && in_game_;
        o.begin();
        o.boolean("active", active);
        o.boolean("shown", active && shown_);
        o.end();
        return 1;
    }
    pulse();
    bool accepted = false;
    if (*operation == "refresh")
        accepted = refresh();
    else if (*operation == "open")
        accepted = command("open");
    else if (*operation == "close") {
        shown_ = false;
        accepted = true;
    } else if (*operation == "resume") {
        const auto revision = services::decimal(host_, args, 1), id = services::decimal(host_, args, 2);
        const auto uuid = services::argument(host_, args, 3, 36);
        if (revision && id && *id <= UINT32_MAX && uuid)
            accepted = resume(*revision, static_cast<std::uint32_t>(*id), *uuid);
    } else if (*operation != "state")
        return 0;
    services::Output o{host_, out};
    if (!o.valid())
        return 0;
    // Host output callbacks may reenter. Publish a copied, coherent view.
    const auto rows = entries_;
    const auto revision = revision_;
    const bool available = context_.has_value(), shown = shown_, pending = pending_ != 0, current = fresh();
    const auto message = message_;
    std::vector<std::uint64_t> times;
    for (const auto &e : rows)
        times.push_back(remaining(e));
    o.begin();
    o.boolean("available", available);
    o.boolean("shown", shown);
    o.boolean("pending", pending);
    o.boolean("fresh", current);
    o.boolean("accepted", accepted);
    o.identity("revision", revision);
    o.string("message", message);
    o.array("entries");
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto &e = rows[i];
        o.begin();
        o.identity("dz_id", e.dz_id);
        o.string("uuid", e.uuid);
        o.string("name", e.name);
        o.string("leader", e.leader);
        o.number("zone_id", e.zone_id);
        o.number("zone_version", e.zone_version);
        o.number("instance_id", e.instance_id);
        o.number("members", e.members);
        o.number("maximum", e.max_members);
        o.identity("remaining_seconds", times[i]);
        o.string("eligibility", result_text(e.eligibility));
        o.boolean("can_rejoin", available && current && e.eligibility == Result::Ok && times[i] != 0);
        o.end();
    }
    o.end();
    o.end();
    return 1;
}
} // namespace bastion::saved_expeditions
