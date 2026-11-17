#include "factions_addon.h"
#include <filesystem>
#include <fstream>
namespace bastion::factions {
void Adapter::retire() {
    context_.reset();
    session_ = services::request_id();
    request_ = character_ = 0;
    shown_ = sent_ = usable_ = assembly_ = false;
    hash_ = 0;
    retry_at_.reset();
    failures_ = 0;
    categories_.clear();
    entries_.clear();
    incoming_categories_.clear();
    incoming_entries_.clear();
    category_seen_.clear();
    entry_seen_.clear();
    revision_ = services::token();
    search_.clear();
    search_revision_ = services::token();
    message_ = "Open Factions to load received standings.";
}
void Adapter::set_host(const PluginHostApi *h) {
    shutdown();
    host_ = h;
}
void Adapter::initialize() {
    if (HAILSTONE_HOST_HAS(host_, add_ui_function))
        host_->add_ui_function(host_->host, "factions", ui, this);
}
void Adapter::shutdown() {
    if (HAILSTONE_HOST_HAS(host_, remove_ui_function))
        host_->remove_ui_function(host_->host, "factions");
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
bool Adapter::fresh() const { return usable_ && now_ >= received_at_ && now_ - received_at_ < 15000; }
void Adapter::failure(std::string message, bool retry) {
    request_ = 0;
    assembly_ = false;
    incoming_categories_.clear();
    incoming_entries_.clear();
    category_seen_.clear();
    entry_seen_.clear();
    usable_ = false;
    message_ = std::move(message);
    retry_at_.reset();
    if (retry && failures_ < 5) {
        const auto delay = std::min<std::uint64_t>(30000, UINT64_C(2500) << failures_);
        ++failures_;
        retry_at_ = now_ > UINT64_MAX - delay ? UINT64_MAX : now_ + delay;
    }
}
void Adapter::pulse() {
    if (!synchronize())
        return;
    if (request_ && (now_ - progress_at_ >= 10000 || now_ - sent_at_ >= 60000))
        failure("Faction transfer timed out. Last received rows may be stale.", true);
    if (shown_ && !request_ && ((retry_at_ && now_ >= *retry_at_) ||
                              (!retry_at_ && usable_ && now_ - received_at_ >= 2500)))
        refresh();
}
bool Adapter::refresh(bool force) {
    if (!synchronize() || !session_ || request_ || (sent_ && now_ - sent_at_ < 1100))
        return false;
    const auto id = services::request_id();
    if (!id)
        return false;
    const auto bytes = request(id, session_, force ? 0 : hash_);
    request_ = id;
    requested_hash_ = force ? 0 : hash_;
    sent_ = true;
    sent_at_ = progress_at_ = now_;
    assembly_ = false;
    retry_at_.reset();
    header_ = {};
    incoming_categories_.clear();
    incoming_entries_.clear();
    category_seen_.clear();
    entry_seen_.clear();
    message_ = "Loading faction standings...";
    const auto context = context_;
    const bool admitted =
        host_->send_packet(host_->host, HAILSTONE_STREAM_ZONE, kOpcode, bytes.data(), bytes.size()) != 0;
    if (!admitted && context_ == context && request_ == id)
        failure("Connection unavailable. Faction refresh will retry with backoff.", true);
    return admitted;
}
void Adapter::publish() {
    if (!validate(incoming_categories_, incoming_entries_, header_.hash)) {
        failure("Faction catalog references or checksum are invalid. Refresh again.", false);
        return;
    }
    categories_ = std::move(incoming_categories_);
    entries_ = std::move(incoming_entries_);
    hash_ = header_.hash;
    character_ = header_.character;
    request_ = 0;
    assembly_ = false;
    usable_ = true;
    received_at_ = now_;
    retry_at_.reset();
    failures_ = 0;
    revision_ = services::token();
    message_ = entries_.empty() ? "No factions were returned." : "Faction standings are current.";
}
bool Adapter::packet(std::uint16_t opcode, const void *bytes, std::size_t size) {
    if (!selected_ || opcode != kOpcode)
        return false;
    pulse();
    if (!context_ || !request_ || (!bytes && size))
        return true;
    const auto p = decode({static_cast<const std::byte *>(bytes), size});
    if (!p || p->request != request_ || p->session != session_)
        return true;
    if (character_ && p->character != character_) {
        failure("The faction reply belongs to a different character.", false);
        return true;
    }
    if (p->kind == Kind::Error) {
        static constexpr const char *errors[] = {"", "Faction data is unavailable.",
                                                 "Faction standings are unavailable while shrouded.",
                                                 "Faction catalog exceeds supported limits.", "The server is busy."};
        failure(errors[p->result], p->result == 1 || p->result == 4);
        return true;
    }
    if (p->kind == Kind::Unchanged) {
        if (assembly_ || !requested_hash_ || p->hash != hash_ || p->hash != requested_hash_ ||
            p->categories != categories_.size() || p->entries != entries_.size()) {
            failure("Faction cache could not be confirmed. Refresh the complete catalog.", false);
            return true;
        }
        request_ = 0;
        usable_ = true;
        received_at_ = now_;
        failures_ = 0;
        message_ = "Faction standings are current.";
        return true;
    }
    if (!assembly_) {
        header_ = *p;
        header_.category_rows.clear();
        header_.entry_rows.clear();
        incoming_categories_.resize(p->categories);
        incoming_entries_.resize(p->entries);
        category_seen_.assign(p->categories, false);
        entry_seen_.assign(p->entries, false);
        assembly_ = true;
    } else if (p->character != header_.character || p->hash != header_.hash || p->categories != header_.categories ||
               p->entries != header_.entries) {
        failure("Faction chunks disagree on snapshot identity.", false);
        return true;
    }
    const bool categories = p->kind == Kind::Categories;
    const auto count = categories ? p->category_rows.size() : p->entry_rows.size();
    auto &seen = categories ? category_seen_ : entry_seen_;
    for (std::size_t i = 0; i < count; ++i)
        if (seen[p->offset + i]) {
            failure("Duplicate faction chunks were rejected.", false);
            return true;
        }
    for (std::size_t i = 0; i < count; ++i) {
        seen[p->offset + i] = true;
        if (categories)
            incoming_categories_[p->offset + i] = p->category_rows[i];
        else
            incoming_entries_[p->offset + i] = p->entry_rows[i];
    }
    progress_at_ = now_;
    if (std::all_of(category_seen_.begin(), category_seen_.end(), [](bool v) { return v; }) &&
        std::all_of(entry_seen_.begin(), entry_seen_.end(), [](bool v) { return v; }))
        publish();
    return true;
}
bool Adapter::command(std::string_view text) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    if (text.size() > 95 || std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        return false;
    if (!synchronize())
        return false;
    std::string action(text);
    for (char &c : action) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    if (action == "export") return export_file();
    if (action.starts_with("export ")) return export_file(text.substr(7));
    if (text.empty()) {
        shown_ = !shown_;
        if (!shown_) return true;
    } else {
        shown_ = true;
        if (action != "open" && action != "refresh") {
            search_ = text;
            search_revision_ = services::token();
        }
    }
    if (action == "refresh" || (!request_ && !fresh())) {
        failures_ = 0;
        refresh(action == "refresh");
    }
    return true;
}
bool Adapter::export_file(std::string_view filename) {
    if (!synchronize() || !fresh() || pending() || !HAILSTONE_HOST_HAS(host_, config_dir)) {
        message_ = "Refresh faction standings before exporting.";
        return false;
    }
    if (filename.size() >= 2 && filename.front() == '"' && filename.back() == '"') {
        filename.remove_prefix(1);
        filename.remove_suffix(1);
    }
    if (filename.empty()) filename = "Factions.txt";
    if (filename.size() > 96 || filename == "." || filename.find("..") != std::string_view::npos ||
        std::any_of(filename.begin(), filename.end(), [](unsigned char c) {
            return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                     c == '-' || c == '_' || c == '.' || c == ' ');
        })) {
        message_ = "Use a plain export filename without folders.";
        return false;
    }
    std::string name(filename);
    if (name.back() == ' ' || name.back() == '.') {
        message_ = "Use a filename ending in a letter, number or .txt.";
        return false;
    }
    std::string stem = name.substr(0, name.find('.'));
    while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) stem.pop_back();
    for (char &c : stem) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
        (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '1' && stem[3] <= '9')) {
        message_ = "That export filename is reserved. Choose another name.";
        return false;
    }
    if (name.size() < 4 || name.substr(name.size() - 4) != ".txt") name += ".txt";
    const auto context = context_;
    const auto revision = revision_;
    const auto entries = entries_;
    char directory[4096]{};
    if (!host_->config_dir(host_->host, directory, sizeof(directory)) ||
        std::find(std::begin(directory), std::end(directory), '\0') == std::end(directory) || !directory[0] ||
        !synchronize() || context != context_ || revision != revision_ || !fresh()) return false;
    try {
        std::u8string utf8_directory;
        for (const char c : std::string_view(directory))
            utf8_directory.push_back(static_cast<char8_t>(static_cast<unsigned char>(c)));
        const auto exports = std::filesystem::path(utf8_directory) / "exports";
        std::filesystem::create_directories(exports);
        const auto path = exports / name;
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "ID\tName\tStandingValue\tPointsToMax\n";
        for (const auto &entry : entries)
            file << entry.id << '\t' << entry.name << '\t' << entry.earned << '\t'
                 << std::max<std::int64_t>(0, std::int64_t(entry.maximum) - entry.earned) << '\n';
        file.close();
        if (!file) throw std::ios_base::failure("export");
        message_ = "Exported all faction standings to the addon settings exports folder: " + name;
        return true;
    } catch (const std::exception &) {
        message_ = "Faction export could not be written to the addon settings folder.";
        return false;
    }
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
        accepted = true;
    } else if (*op == "refresh") {
        failures_ = 0;
        accepted = refresh(true);
    } else if (*op == "export") {
        const auto name = services::argument(host_, args, 1, 100);
        accepted = export_file(name ? *name : "");
    } else if (*op != "state")
        return 0;
    services::Output o{host_, out};
    if (!o.valid())
        return 0;
    const auto cached = services::decimal(host_, args, 1);
    const bool unchanged = cached && *cached == revision_;
    const auto categories = unchanged ? std::vector<Category>{} : categories_;
    const auto entries = unchanged ? std::vector<Entry>{} : entries_;
    const auto message = message_;
    const auto revision = revision_;
    const bool available = context_.has_value(), shown = shown_, pending = request_ != 0, current = fresh();
    o.begin();
    o.boolean("available", available);
    o.boolean("shown", shown);
    o.boolean("pending", pending);
    o.boolean("fresh", current);
    o.boolean("accepted", accepted);
    o.identity("revision", revision);
    o.string("message", message);
    o.string("search", search_);
    o.identity("search_revision", search_revision_);
    o.boolean("unchanged", unchanged);
    if (unchanged) {
        o.end();
        return 1;
    }
    o.array("categories");
    for (const auto &c : categories) {
        o.begin();
        o.number("id", c.id);
        o.number("parent", c.parent);
        o.number("order", c.order);
        o.string("name", c.name);
        o.string("icon", c.icon);
        o.end();
    }
    o.end();
    o.array("entries");
    for (const auto &e : entries) {
        o.begin();
        o.number("id", e.id);
        o.string("name", e.name);
        o.number("earned", e.earned);
        o.number("minimum", e.minimum);
        o.number("maximum", e.maximum);
        o.number("effective", e.effective);
        o.number("next_threshold", e.next);
        o.number("reaction", e.reaction);
        o.string("standing", reaction_name(e.reaction));
        o.number("gauge", gauge(e));
        o.array("categories");
        for (auto id : e.categories) {
            HailstoneValue v{};
            v.kind = HAILSTONE_INT;
            v.i = id;
            host_->out_value(host_->host, out, &v);
        }
        o.end();
        o.end();
    }
    o.end();
    o.end();
    return 1;
}
} // namespace bastion::factions
