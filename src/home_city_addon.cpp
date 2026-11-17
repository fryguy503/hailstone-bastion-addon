#include "home_city_addon.h"
namespace bastion::home_city {
void Adapter::retire() {
    context_.reset();
    pending_ = 0;
    requested_choice_ = preselected_choice_ = 0;
    selection_revision_ = services::token();
    sent_ = usable_ = shown_ = false;
    safe_since_.reset();
    next_probe_.reset();
    state_ = {};
    revision_ = services::token();
    message_ = "Refresh to load available home city choices.";
}
void Adapter::set_host(const PluginHostApi *h) {
    shutdown();
    host_ = h;
}
void Adapter::initialize() {
    if (HAILSTONE_HOST_HAS(host_, add_ui_function))
        host_->add_ui_function(host_->host, "home_city", ui, this);
}
void Adapter::shutdown() {
    if (HAILSTONE_HOST_HAS(host_, remove_ui_function))
        host_->remove_ui_function(host_->host, "home_city");
    retire();
    selected_ = in_game_ = false;
}
void Adapter::peer(bool value) {
    selected_ = value;
    retire();
    shown_grants_.clear();
    reminder_owner_.clear();
}
void Adapter::game_state(int value) {
    in_game_ = value == GAMESTATE_INGAME;
    if (!in_game_)
        retire();
    if (value == GAMESTATE_PRECHARSELECT || value == GAMESTATE_CHARSELECT || value == GAMESTATE_CHARCREATE ||
        value == GAMESTATE_LOGGINGIN || value == GAMESTATE_UNLOADING) {
        shown_grants_.clear();
        reminder_owner_.clear();
    }
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
    if (reminder_owner_ != current->name) {
        shown_grants_.clear();
        reminder_owner_ = current->name;
    }
    const auto time = host_->monotonic_ms(host_->host);
    if (time < now_) {
        retire();
        context_ = current;
    }
    now_ = time;
    return true;
}
bool Adapter::fresh() const { return usable_ && !pending_ && now_ >= received_at_ && now_ - received_at_ < 240000; }
void Adapter::pulse() {
    if (!synchronize())
        return;
    if (pending_ && now_ - sent_at_ >= 10000) {
        pending_ = 0;
        requested_choice_ = 0;
        usable_ = false;
        message_ = action_ == Action::Commit
                       ? "No change reply received. Refresh to check your home and grant; the change was not retried."
                       : "No reply received. Refresh when the server is ready.";
    }
    if (usable_ && now_ - received_at_ >= 240000) {
        usable_ = false;
        revision_ = services::token();
        message_ = "The city choices expired. Refresh before confirming.";
    }
    if (shown_ && requested_choice_ && !pending_ && (!sent_ || now_ - sent_at_ >= 1100))
        refresh();
    HailstonePlayer player{};
    player.size = sizeof(player);
    const bool safe = host_->get_player(host_->host, &player) &&
                      player.size >= offsetof(HailstonePlayer, observation_flags) + sizeof(player.observation_flags) &&
                      (player.observation_flags & HAILSTONE_PLAYER_PROMPT_SAFE) != 0;
    if (!safe) {
        safe_since_.reset();
        return;
    }
    if (!safe_since_)
        safe_since_ = now_;
    if (!shown_ && !pending_ && now_ - *safe_since_ >= 5000 && (!next_probe_ || now_ >= *next_probe_)) {
        next_probe_ = now_ > UINT64_MAX - 30000 ? UINT64_MAX : now_ + 30000;
        refresh(true);
    }
}
bool Adapter::send(Action action, std::uint32_t choice) {
    if (!synchronize() || pending_ || (sent_ && now_ - sent_at_ < 1100))
        return false;
    const bool mutation = action > Action::List;
    if (mutation && !fresh())
        return false;
    std::uint32_t home = 0;
    if (action == Action::Commit) {
        for (const auto &e : state_.entries)
            if (e.choice == choice && e.zone != state_.current_home)
                home = e.zone;
        if (!home)
            return false;
    }
    const auto id = services::request_id();
    const auto bytes = request(action, id, choice, mutation ? state_.grant : 0, mutation ? state_.snapshot : 0);
    if (bytes.empty())
        return false;
    expected_grant_ = mutation ? state_.grant : 0;
    expected_home_ = home;
    pending_ = id;
    action_ = action;
    sent_ = true;
    sent_at_ = now_;
    usable_ = false;
    message_ = action == Action::Commit     ? "Saving the selected home city..."
               : action == Action::Suppress ? "Saving reminder preference..."
                                            : "Loading home city choices...";
    const auto context = context_;
    const bool admitted =
        host_->send_packet(host_->host, HAILSTONE_STREAM_ZONE, kOpcode, bytes.data(), bytes.size()) != 0;
    if (!admitted && context_ == context && pending_ == id) {
        pending_ = 0;
        message_ = "Connection unavailable. Refresh before trying again.";
    }
    return admitted;
}
bool Adapter::refresh(bool probe) { return send(probe ? Action::Probe : Action::List); }
bool Adapter::choose(std::uint64_t revision, std::uint32_t choice, std::uint64_t grant, std::uint64_t snapshot) {
    pulse();
    return revision == revision_ && grant == state_.grant && snapshot == state_.snapshot &&
           send(Action::Commit, choice);
}
bool Adapter::suppress(std::uint64_t revision, std::uint64_t grant, std::uint64_t snapshot) {
    pulse();
    return revision == revision_ && grant == state_.grant && snapshot == state_.snapshot && send(Action::Suppress);
}
bool Adapter::packet(std::uint16_t opcode, const void *data, std::size_t size) {
    if (!selected_ || opcode != kOpcode)
        return false;
    pulse();
    if (!context_ || !pending_ || (!data && size))
        return true;
    const auto value = decode({static_cast<const std::byte *>(data), size});
    if (!value || value->request != pending_ || value->action != action_)
        return true;
    if (value->result == Result::Ok && action_ > Action::List &&
        (value->grant != expected_grant_ || (action_ == Action::Commit && value->current_home != expected_home_) ||
         (action_ == Action::Suppress && !(value->flags & 1u))))
        return true;
    pending_ = 0;
    usable_ = false;
    revision_ = services::token();
    state_ = *value;
    message_ = value->message.empty() ? "No home city change was confirmed." : value->message;
    if (value->result == Result::Ok && action_ <= Action::List && value->grant && value->snapshot && value->race &&
        value->class_id && value->class_id <= 16 && !value->entries.empty()) {
        usable_ = true;
        received_at_ = now_;
        message_ = "Choose a returned city, then review and confirm. Your ordinary bind is unchanged.";
        if (action_ == Action::Probe && !(value->flags & 1u) && !shown_grants_.contains(value->grant) &&
            shown_grants_.size() < 64 && std::any_of(value->entries.begin(), value->entries.end(), [&](const Entry &e) {
                return e.zone != value->current_home;
            })) {
            shown_ = true;
            shown_grants_.insert(value->grant);
        } else if (action_ == Action::List && shown_grants_.size() < 64)
            shown_grants_.insert(value->grant);
    } else if (value->result == Result::Ok && action_ == Action::Commit)
        message_ = "Home city changed. The free reset has been used; your ordinary bind is unchanged.";
    else if (value->result == Result::Ok && action_ == Action::Suppress)
        message_ = "Reminders for this reset are disabled. You can still open Home City manually.";
    if (action_ <= Action::List) {
        if (usable_) apply_requested_choice();
        else if (requested_choice_) {
            requested_choice_ = 0;
            report("The requested home city is unavailable. " + message_);
        }
    }
    return true;
}
void Adapter::report(std::string text) {
    message_ = std::move(text);
    if (HAILSTONE_HOST_HAS(host_, write_chat)) host_->write_chat(host_->host, message_.c_str(), 0);
}
void Adapter::apply_requested_choice() {
    if (!requested_choice_ || !usable_) return;
    const auto choice = requested_choice_;
    requested_choice_ = 0;
    const auto found = std::find_if(state_.entries.begin(), state_.entries.end(),
                                    [choice](const Entry &entry) { return entry.choice == choice; });
    preselected_choice_ = found == state_.entries.end() ? 0 : choice;
    selection_revision_ = services::token();
    if (!preselected_choice_) report("Home city ID " + std::to_string(choice) + " is not available in the returned choices.");
    else message_ = "Requested city selected. Review and confirm before changing your home.";
}
bool Adapter::command(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    std::string action(text);
    for (char &c : action) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    std::uint32_t choice = 0;
    const bool control = action.empty() || action == "open" || action == "refresh" || action == "probe";
    if (!control) {
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), choice);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !choice) {
            report("Usage: /setstartcity [city ID]. Select a returned city, then review and confirm.");
            return false;
        }
    }
    if (!synchronize())
        return false;
    shown_ = true;
    requested_choice_ = choice;
    if (choice) {
        preselected_choice_ = 0;
        selection_revision_ = services::token();
        if (fresh()) apply_requested_choice();
    }
    if (action == "refresh" || action == "probe" || (!pending_ && !fresh()))
        refresh(action == "probe");
    return true;
}
int Adapter::ui(void *self, const HailstoneArgs *args, HailstoneOut *out) noexcept {
    if (!self) return 0;
    auto &owner = *static_cast<Adapter*>(self);
    return services::guarded_ui(owner.host_, [&] { return owner.call(args,out); });
}
int Adapter::call(const HailstoneArgs *args, HailstoneOut *out) {
    const auto op = services::argument(host_, args, 0, 16);
    if (!op)
        return 0;
    if (*op == "visibility") {
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
    if (*op == "open")
        accepted = command("open");
    else if (*op == "close") {
        shown_ = false;
        requested_choice_ = 0;
        accepted = true;
    } else if (*op == "refresh")
        accepted = refresh();
    else if (*op == "commit" || *op == "suppress") {
        const auto revision = services::decimal(host_, args, 1), grant = services::decimal(host_, args, 2),
                   snapshot = services::decimal(host_, args, 3);
        if (revision && grant && snapshot) {
            if (*op == "suppress")
                accepted = suppress(*revision, *grant, *snapshot);
            else if (const auto choice = services::decimal(host_, args, 4); choice && *choice <= UINT32_MAX)
                accepted = choose(*revision, static_cast<std::uint32_t>(*choice), *grant, *snapshot);
        }
    } else if (*op != "state")
        return 0;
    services::Output o{host_, out};
    if (!o.valid())
        return 0;
    const auto state = state_;
    const auto revision = revision_;
    const auto message = message_;
    const bool available = context_.has_value(), shown = shown_, pending = pending_ != 0, current = fresh();
    o.begin();
    o.boolean("available", available);
    o.boolean("shown", shown);
    o.boolean("pending", pending);
    o.boolean("fresh", current);
    o.boolean("accepted", accepted);
    o.identity("revision", revision);
    o.identity("grant", state.grant);
    o.identity("snapshot", state.snapshot);
    o.identity("preselected_choice", preselected_choice_);
    o.identity("selection_revision", selection_revision_);
    o.number("race", state.race);
    o.number("class_id", state.class_id);
    o.number("deity", state.deity);
    o.number("current_home", state.current_home);
    o.boolean("suppressed", (state.flags & 1u) != 0);
    o.string("message", message);
    o.array("entries");
    for (const auto &e : state.entries) {
        o.begin();
        o.identity("choice", e.choice);
        o.number("zone_id", e.zone);
        o.string("name", e.name);
        o.boolean("can_choose", available && current && e.zone != state.current_home);
        o.end();
    }
    o.end();
    o.end();
    return 1;
}
} // namespace bastion::home_city
