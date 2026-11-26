// SPDX-License-Identifier: MIT
#include "expedition_journal.h"

#include <algorithm>

namespace bastion::journal {
namespace {
using Json = nlohmann::json;
using services::Output;

constexpr std::uint32_t kMagic = 0x31524a42; // BJR1
constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kMaxReply = 16 * 1024;
constexpr std::uint64_t kRequestTimeoutMs = 6500;
constexpr std::uint64_t kRequestSpacingMs = 1100;
constexpr std::uint64_t kSnapshotLifeMs = 30000;
constexpr std::uint64_t kContextPollMs = 250;
// The published entry guides name these four keys, the shared Writ, and the ToFS offering.
constexpr std::uint32_t kPublicItems[] = {147700, 147701, 147702, 147703, 147760, 148252};

const Json& field(const Json& object, std::string_view name) {
    static const Json missing;
    if (!object.is_object()) return missing;
    const auto found = object.find(name);
    return found == object.end() ? missing : *found;
}

bool named_item(const Json& object, std::string_view id_key, std::string_view name_key, std::uint32_t id) {
    const auto* name = field(object, name_key).get_ptr<const Json::string_t*>();
    return name && !name->empty() && field(object, id_key) == id;
}

// The journal consumes read-only server snapshots. No response can provide an
// executable action, and unsupported JSON values never cross the UI boundary.
void write_value(const Output& result, const Json& value, std::optional<std::string_view> key = {}) {
    HailstoneValue scalar{};
    std::string text;
    if (value.is_boolean()) {
        scalar.kind = HAILSTONE_BOOL;
        scalar.i = value == true ? 1 : 0;
    } else if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > INT64_MAX) return;
        scalar.kind = HAILSTONE_INT;
        scalar.i = static_cast<std::int64_t>(number);
    } else if (value.is_number_integer()) {
        scalar.kind = HAILSTONE_INT;
        scalar.i = value.get<std::int64_t>();
    } else if (const auto* string = value.get_ptr<const Json::string_t*>()) {
        text = *string;
        std::erase_if(text, [](unsigned char c) { return c < 32 || c == 127; });
        scalar.kind = HAILSTONE_STRING;
        scalar.s = text.c_str();
    } else if (!value.is_structured()) {
        return;
    }
    if (key) {
        if (key->empty() || key->find('\0') != key->npos) return;
        const std::string name(*key);
        result.key(name.c_str());
    }
    if (value.is_object()) {
        result.begin();
        for (auto item = value.begin(); item != value.end(); ++item)
            write_value(result, item.value(), item.key());
        result.end();
    } else if (value.is_array()) {
        result.host->out_begin_array(result.host->host, result.out);
        for (const auto& item : value)
            write_value(result, item);
        result.end();
    } else {
        result.host->out_value(result.host->host, result.out, &scalar);
    }
}
} // namespace

void Adapter::set_host(const PluginHostApi* host) {
    if (HAILSTONE_HOST_HAS(host_, remove_ui_function))
        host_->remove_ui_function(host_->host, "expedition_journal");
    host_ = host;
    selected_ = in_game_ = false;
    close();
}

void Adapter::initialize() {
    if (HAILSTONE_HOST_HAS(host_, add_ui_function))
        host_->add_ui_function(host_->host, "expedition_journal", callback, this);
}

void Adapter::changed() { revision_ = services::token(); }

void Adapter::close() {
    shown_ = false;
    wanted_ = Query{Mode::Normal};
    reset_session();
}

void Adapter::reset_session() {
    context_.reset();
    queued_.reset();
    pending_.reset();
    sent_ = false;
    next_context_at_ = 0;
    entries_ = {};
    generation_ = services::token();
    changed();
}

void Adapter::peer(bool selected) {
    if (selected == selected_) return;
    selected_ = selected;
    in_game_ = false;
    close();
}

void Adapter::game_state(int state) {
    in_game_ = state == GAMESTATE_INGAME;
    if (in_game_) {
        next_context_at_ = 0;
    } else if (state == GAMESTATE_LOGGINGIN) {
        reset_session();
    } else {
        close();
    }
}

void Adapter::begin_zone() {
    in_game_ = false;
    reset_session();
}

bool Adapter::open() {
    if (!selected_ || !in_game_) return false;
    if (!shown_) {
        shown_ = true;
        synchronize(true);
        changed();
    }
    return true;
}

void Adapter::synchronize(bool force) {
    if (!HAILSTONE_HOST_HAS(host_, monotonic_ms)) return;
    const auto time = host_->monotonic_ms(host_->host);
    if (time < now_) reset_session();
    now_ = time;
    if (!force && now_ < next_context_at_) return;
    auto current = services::context(host_, selected_, in_game_);
    if (current != context_) {
        reset_session();
        context_ = std::move(current);
        if (context_) queued_ = wanted_;
    }
    next_context_at_ = now_ + kContextPollMs;
}

std::string_view Adapter::mode_name(Mode mode) {
    switch (mode) {
        case Mode::Normal: return "normal";
        case Mode::Bloodbound: return "hard";
        case Mode::Vigil: return "vigil";
        case Mode::Copium: return "copium";
        case Mode::Sleepers: return "sleepers";
        case Mode::TofsGroup: return "tofs_group";
        case Mode::TofsRaid: return "tofs_raid";
        case Mode::BrokenAccord: return "broken_accord";
        case Mode::Firefell: return "firefell";
    }
    return {};
}

std::size_t Adapter::index(const Query& query) {
    return query.relic ? kRelicDetailIndex : static_cast<std::size_t>(query.mode) - 1;
}

std::optional<Adapter::Query> Adapter::query_from(const HailstoneArgs* args) const {
    const auto name = services::argument(host_, args, 1, 16);
    if (!name) return std::nullopt;
    for (const auto mode : {Mode::Normal, Mode::Bloodbound, Mode::Vigil, Mode::Copium, Mode::Sleepers,
                            Mode::TofsGroup, Mode::TofsRaid, Mode::BrokenAccord, Mode::Firefell}) {
        if (*name != mode_name(mode)) continue;
        std::uint64_t relic = 0;
        HailstoneValue argument{};
        if (host_->arg(host_->host, args, 2, &argument) && argument.kind != HAILSTONE_NIL) {
            if (argument.kind == HAILSTONE_INT && argument.i >= 0) {
                relic = static_cast<std::uint64_t>(argument.i);
            } else {
                const auto parsed = services::decimal(host_, args, 2);
                if (!parsed) return std::nullopt;
                relic = *parsed;
            }
        }
        if (relic > INT32_MAX || (relic != 0 && mode != Mode::Copium)) return std::nullopt;
        return Query{mode, static_cast<std::uint32_t>(relic)};
    }
    return std::nullopt;
}

void Adapter::select(const Query& query, bool refresh) {
    if (!shown_) return;
    synchronize();
    if (!refresh && query == wanted_) return;
    wanted_ = query;
    if (!context_) return;
    if (pending_ && pending_->query == query) return;
    pending_.reset();
    queued_.reset();
    const auto& entry = entries_[index(query)];
    if (refresh || entry.relic != query.relic || entry.data.is_null())
        queued_ = query;
    changed();
}

void Adapter::unavailable(const Query& query) {
    auto& entry = entries_[index(query)];
    entry = {};
    entry.relic = query.relic;
    entry.unavailable = true;
    changed();
}

void Adapter::send_query() {
    if (!context_ || !queued_ || pending_ || (sent_ && now_ - last_send_ < kRequestSpacingMs))
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
    if (pending_ && now_ - pending_->sent_at >= kRequestTimeoutMs) {
        unavailable(pending_->query);
        pending_.reset();
    }
    for (auto& entry : entries_) {
        if (!entry.data.is_null() && now_ - entry.received_at >= kSnapshotLifeMs) {
            entry.data = nullptr;
            entry.unavailable = true;
            changed();
        }
    }
    send_query();
}

bool Adapter::packet(std::uint16_t opcode, const void* data, std::size_t size) {
    if (!selected_ || opcode != kOpcode) return false;
    if (!shown_ || !pending_) return true;
    synchronize(true);
    if (!context_ || !pending_ || !data || size <= kHeaderSize || size > kHeaderSize + kMaxReply)
        return true;
    const auto [query, id, sent_at] = *pending_;
    if (now_ - sent_at >= kRequestTimeoutMs) return true;
    hailstone::ByteReader reader(data, size);
    if (reader.le<std::uint32_t>() != kMagic || reader.le<std::uint16_t>() != kVersion ||
        reader.le<std::uint16_t>() != static_cast<std::uint16_t>(query.mode) || reader.le<std::uint32_t>() != id)
        return true;
    const auto length = reader.le<std::uint32_t>();
    if (length != reader.remaining()) return true;
    const auto body = reader.bytes(length);
    const std::string_view text(reinterpret_cast<const char*>(body.data()), body.size());
    if (text.find('\0') != text.npos) return true;
    auto message = Json::parse(text, [](int depth, Json::parse_event_t, Json&) { return depth <= 8; }, false);
    auto state = message.is_object() ? message.find("state") : message.end();
    const auto& version = field(message, "version");
    const auto* mode = field(message, "mode").get_ptr<const Json::string_t*>();
    if (state == message.end() || !state->is_object() || field(message, "type") != "expeditionJournalStatus" ||
        !version.is_number_integer() || version != kVersion || !mode || std::string_view(*mode) != mode_name(query.mode))
        return true;
    if (query.mode == Mode::Copium) {
        if (query.relic) {
            if (field(field(*state, "relic"), "relicId") != query.relic) return true;
        } else if (!field(*state, "relics").is_array()) {
            return true;
        }
    }
    entries_[index(query)] = Entry{query.relic, now_, std::move(*state), false};
    pending_.reset();
    changed();
    return true;
}

void Adapter::write_snapshot(const Output& result, const Query& query) const {
    const auto& entry = entries_[index(query)];
    const bool pending = queued_ == query || (pending_ && pending_->query == query);
    const bool ready = entry.relic == query.relic && !entry.data.is_null() &&
                       now_ - entry.received_at < kSnapshotLifeMs;
    const bool unavailable = entry.relic == query.relic && entry.unavailable;
    result.boolean("pending", pending);
    result.string("status", pending ? "loading" : ready ? "ready" : unavailable || !context_ ? "unavailable" : "idle");
    if (ready) {
        result.number("ageMs", static_cast<std::int64_t>(now_ - entry.received_at));
        write_value(result, entry.data, "data");
    }
}

bool Adapter::can_inspect_item() const {
    return shown_ && context_.has_value() && HAILSTONE_HOST_HAS(host_, inspect_item);
}

bool Adapter::inspect_item(std::uint32_t id) {
    synchronize(true);
    if (!can_inspect_item()) return false;
    bool permitted = std::find(std::begin(kPublicItems), std::end(kPublicItems), id) != std::end(kPublicItems);
    if (!permitted && wanted_.mode == Mode::Copium) {
        const auto& entry = entries_[kRelicDetailIndex];
        if (entry.relic == wanted_.relic && !entry.data.is_null() && now_ - entry.received_at < kSnapshotLifeMs) {
            const auto& relic = field(entry.data, "relic");
            permitted = named_item(relic, "relicId", "relicName", id);
            const auto& rewards = field(relic, "rewards");
            if (!permitted && rewards.is_array()) {
                permitted = std::any_of(rewards.begin(), rewards.end(), [id](const auto& reward) {
                    return named_item(reward, "itemId", "name", id);
                });
            }
        }
    }
    return permitted && host_->inspect_item(host_->host, id, nullptr);
}

int Adapter::callback(void* user, const HailstoneArgs* args, HailstoneOut* out) noexcept {
    auto& self = *static_cast<Adapter*>(user);
    return services::guarded_ui(self.host_, [&] { return self.ui(args, out); });
}

int Adapter::ui(const HailstoneArgs* args, HailstoneOut* out) {
    const auto action = services::argument(host_, args, 0);
    const Output result{host_, out};
    if (!action || !result.valid()) return 0;
    if (*action == "window") {
        result.begin();
        result.boolean("shown", shown_);
        result.boolean("connected", context_.has_value());
        result.boolean("canInspectItem", can_inspect_item());
        if (context_) result.string("character", context_->name);
        result.identity("generation", generation_);
        result.identity("revision", revision_);
        result.end();
        return 1;
    }
    if (*action == "close") {
        close();
        return 1;
    }
    if (*action == "inspect_item") {
        const auto id = services::decimal(host_, args, 1);
        return id && *id > 0 && *id <= UINT32_MAX && inspect_item(static_cast<std::uint32_t>(*id)) ? 1 : 0;
    }
    const auto query = query_from(args);
    if (!query || !shown_) return 0;
    if (*action == "snapshot") {
        result.begin();
        write_snapshot(result, *query);
        result.end();
        return 1;
    }
    if (*action != "select" && *action != "refresh") return 0;
    select(*query, *action == "refresh");
    return 1;
}

} // namespace bastion::journal
