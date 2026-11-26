// SPDX-License-Identifier: MIT
#include "expedition_journal.h"
#include "expedition_journal_bundle.inc"

namespace bastion::journal {
namespace {

constexpr char kJournalUrl[] = "https://bastiongame.com/library/game/expedition-journal";
constexpr char kLibraryUrl[] = "https://bastiongame.com/library/encounters";
constexpr char kLibraryCommand[] = "/url https://bastiongame.com/library/encounters";
constexpr std::uint32_t kMagic = 0x31524a42; // BJR1
constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kMaxReply = 16 * 1024;
constexpr std::uint64_t kOpenTimeout = 10000;
constexpr std::uint64_t kBundleDelay = 1800;
constexpr std::uint64_t kRequestTimeout = 6500;
constexpr std::uint64_t kRequestSpacing = 1100;

// Encode bytes for a JS string. UTF-8 is preserved for the browser, and all ASCII
// controls and delimiters are escaped before this text crosses the script boundary.
std::string quote(std::string_view text) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result = "\"";
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c < 32 || c == 127) {
            result += "\\u00";
            result += digits[c >> 4];
            result += digits[c & 15];
        } else {
            result += static_cast<char>(c);
        }
    }
    result += '"';
    return result;
}

} // namespace

void Adapter::set_host(const PluginHostApi* host) {
    close_tab();
    if (HAILSTONE_HOST_HAS(host_, remove_ui_function))
        host_->remove_ui_function(host_->host, "expedition_journal");
    host_ = host;
    selected_ = in_game_ = shown_ = false;
    context_.reset();
}

void Adapter::initialize() {
    if (HAILSTONE_HOST_HAS(host_, add_ui_function))
        host_->add_ui_function(host_->host, "expedition_journal", callback, this);
}

void Adapter::close_tab() {
    if (tab_ && HAILSTONE_HOST_HAS(host_, web_close))
        host_->web_close(host_->host, tab_);
    tab_ = 0;
    ready_ = opening_ = false;
    bundle_sent_ = using_bundle_ = false;
    queued_.reset();
    pending_.reset();
    reply_.clear();
    last_library_open_.reset();
}

void Adapter::peer(bool selected) {
    if (selected == selected_) return;
    close_tab();
    selected_ = selected;
    shown_ = in_game_ = false;
    context_.reset();
}

void Adapter::game_state(int state) {
    in_game_ = state == GAMESTATE_INGAME;
    if (!in_game_) {
        reset_session();
        if (state != GAMESTATE_LOGGINGIN) {
            shown_ = false;
            close_tab();
        }
    }
}

void Adapter::begin_zone() {
    in_game_ = false;
    reset_session();
}

void Adapter::reset_session() {
    if (context_ && ready_)
        notify(R"({"type":"expeditionJournalUnavailable","version":1})");
    context_.reset();
    queued_.reset();
    pending_.reset();
    reply_.clear();
    sent_ = false;
}

void Adapter::synchronize() {
    if (HAILSTONE_HOST_HAS(host_, monotonic_ms))
        now_ = host_->monotonic_ms(host_->host);
    const auto current = services::context(host_, selected_, in_game_);
    if (current == context_) return;
    reset_session();
    context_ = current;
    if (context_ && ready_)
        notify(R"({"type":"expeditionJournalAvailable","version":1})");
}

void Adapter::browse(bool backup) {
    close_tab();
    backup_ = backup;
    if (!HAILSTONE_HOST_HAS(host_, web_open) || !HAILSTONE_HOST_HAS(host_, web_message) ||
        !HAILSTONE_HOST_HAS(host_, web_eval) || !HAILSTONE_HOST_HAS(host_, web_state))
        return;
    tab_ = host_->web_open(host_->host, page_url().data(), 1024, 760);
    opening_ = tab_ != 0;
    opened_at_ = now_;
}

bool Adapter::open() {
    if (!selected_ || !in_game_) return false;
    synchronize();
    if (!shown_) {
        shown_ = true;
        browse();
    }
    return true;
}

bool Adapter::trusted_page() const {
    if (!tab_ || !HAILSTONE_HOST_HAS(host_, web_state)) return false;
    HailstoneWebState state{};
    state.size = sizeof(state);
    return host_->web_state(host_->host, tab_, &state) != 0 && !state.loading && !state.closed &&
           services::bounded(state.url) == page_url();
}

std::string_view Adapter::page_url() const {
    return backup_ ? kLibraryUrl : kJournalUrl;
}

bool Adapter::notify(std::string_view payload) const {
    if (!ready_ || !trusted_page()) return false;
    // The renderer rechecks the destination at execution time as a navigation
    // can occur after the host's last observed address.
    std::string script = "if(location.href===" + quote(page_url());
    if (using_bundle_) script += "&&window.bastionExpeditionJournalBundled===true";
    script += "&&typeof window.hailstone?.onmessage==='function')window.hailstone.onmessage(" + quote(payload) + ");";
    return host_->web_eval(host_->host, tab_, script.c_str()) != 0;
}

std::string_view Adapter::mode_name(Mode mode) {
    switch (mode) {
        case Mode::Normal: return "normal";
        case Mode::Bloodbound: return "hard";
        case Mode::Vigil: return "vigil";
        case Mode::Copium: return "copium";
    }
    return {};
}

std::optional<Adapter::Query> Adapter::parse_query(std::string_view message) {
    constexpr std::string_view prefix = "BJR1:refresh:";
    if (!message.starts_with(prefix)) return std::nullopt;
    message.remove_prefix(prefix.size());
    if (message == "normal") return Query{Mode::Normal};
    if (message == "hard") return Query{Mode::Bloodbound};
    if (message == "vigil") return Query{Mode::Vigil};
    if (message == "copium") return Query{Mode::Copium};
    constexpr std::string_view detail = "copium:";
    if (!message.starts_with(detail)) return std::nullopt;
    message.remove_prefix(detail.size());
    std::uint32_t relic = 0;
    const auto end = message.data() + message.size();
    const auto parsed = std::from_chars(message.data(), end, relic);
    if (parsed.ec != std::errc{} || parsed.ptr != end || relic == 0) return std::nullopt;
    return Query{Mode::Copium, relic};
}

void Adapter::unavailable(const Query& query) const {
    std::string message = R"({"type":"expeditionJournalUnavailable","version":1,"mode":")";
    message += mode_name(query.mode);
    message += '"';
    if (query.relic)
        message += ",\"relicId\":" + std::to_string(query.relic);
    message += '}';
    notify(message);
}

void Adapter::read_page_messages() {
    char message[64]{};
    for (int count = 0; count < 8; ++count) {
        const int length = host_->web_message(host_->host, tab_, message, sizeof(message));
        if (length < 0) break;
        if (length >= static_cast<int>(sizeof(message))) continue;
        const std::string_view command(message, static_cast<std::size_t>(length));
        const bool bundled = command == "BJR1:bundled-ready" && bundle_sent_;
        if (bundled || (command == "BJR1:ready" && !backup_ && !bundle_sent_)) {
            ready_ = true;
            opening_ = false;
            using_bundle_ = bundled;
            continue;
        }
        if (!ready_) continue;
        if (command == "BJR1:open-library") {
            if (HAILSTONE_HOST_HAS(host_, do_command) && (!last_library_open_ || now_ - *last_library_open_ >= 1500)) {
                last_library_open_ = now_;
                host_->do_command(host_->host, kLibraryCommand);
            }
            continue;
        }
        if (const auto query = parse_query(command)) {
            if (!context_) unavailable(*query);
            else if (pending_ && pending_->query == *query) queued_.reset();
            else queued_ = query;
        }
    }
}

void Adapter::send_query() {
    if (!context_ || !queued_ || pending_ || (sent_ && now_ - last_send_ < kRequestSpacing))
        return;
    const Query query = *queued_;
    queued_.reset();
    const auto id = services::request_id();
    if (!id) {
        unavailable(query);
        return;
    }
    hailstone::ByteWriter request;
    request.reserve(kHeaderSize);
    request.le(kMagic);
    request.le(kVersion);
    request.le(static_cast<std::uint16_t>(query.mode));
    request.le(id);
    request.le(query.relic);
    pending_ = Pending{query, id, now_};
    sent_ = true;
    last_send_ = now_;
    if (!host_->send_packet(host_->host, HAILSTONE_STREAM_ZONE, kOpcode, request.data().data(), request.size()) &&
        pending_ && pending_->id == id) {
        pending_.reset();
        unavailable(query);
    }
}

void Adapter::pulse() {
    if (!shown_) return;
    synchronize();
    if (!tab_) return;
    if (!trusted_page()) {
        if (ready_) close_tab();
        else if (now_ - opened_at_ >= kOpenTimeout) {
            if (backup_) close_tab();
            else browse(true);
        }
        return;
    }
    read_page_messages();
    if (opening_ && now_ - opened_at_ >= kOpenTimeout) {
        if (backup_) close_tab();
        else browse(true);
        return;
    }
    if (opening_ && !bundle_sent_ && now_ - opened_at_ >= (backup_ ? 0u : kBundleDelay)) {
        bundle_sent_ = host_->web_eval(host_->host, tab_, kExpeditionJournalFallbackScript) != 0;
    }
    if (!reply_.empty()) {
        if (notify(reply_)) reply_.clear();
        else return;
    }
    if (pending_ && now_ - pending_->sent_at >= kRequestTimeout) {
        unavailable(pending_->query);
        pending_.reset();
    }
    send_query();
}

bool Adapter::packet(std::uint16_t opcode, const void* data, std::size_t size) {
    if (!selected_ || opcode != kOpcode) return false;
    synchronize();
    if (!context_ || !pending_ || !ready_ || !trusted_page() || !data ||
        size <= kHeaderSize || size > kHeaderSize + kMaxReply) return true;
    hailstone::ByteReader reader(data, size);
    if (reader.le<std::uint32_t>() != kMagic || reader.le<std::uint16_t>() != kVersion ||
        reader.le<std::uint16_t>() != static_cast<std::uint16_t>(pending_->query.mode) ||
        reader.le<std::uint32_t>() != pending_->id)
        return true;
    const auto length = reader.le<std::uint32_t>();
    if (length != reader.remaining()) return true;
    const auto body = reader.bytes(length);
    const std::string_view json(reinterpret_cast<const char*>(body.data()), body.size());
    if (json.front() != '{' || json.back() != '}' || json.find('\0') != json.npos) return true;
    reply_ = json;
    pending_.reset();
    return true;
}

int Adapter::callback(void* user, const HailstoneArgs* args, HailstoneOut* out) noexcept {
    auto& self = *static_cast<Adapter*>(user);
    return services::guarded_ui(self.host_, [&] { return self.ui(args, out); });
}

int Adapter::ui(const HailstoneArgs* args, HailstoneOut* out) {
    const auto action = services::argument(host_, args, 0);
    const services::Output result{host_, out};
    if (!action || !result.valid()) return 0;
    if (*action == "close") {
        shown_ = false;
        close_tab();
    } else if (*action == "retry" && shown_ && selected_ && in_game_) {
        browse();
    } else if (*action == "open_library" && shown_ && HAILSTONE_HOST_HAS(host_, do_command)) {
        host_->do_command(host_->host, kLibraryCommand);
    } else if (*action != "window") {
        return 0;
    }
    result.begin();
    result.boolean("shown", shown_);
    result.boolean("opening", opening_);
    result.number("tab", tab_);
    result.end();
    return 1;
}

} // namespace bastion::journal
