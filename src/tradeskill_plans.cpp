// SPDX-License-Identifier: MIT
#include "tradeskill_plans.h"

#include <array>
#include <fstream>
#include <random>
#include <system_error>

namespace bastion::tradeskill_plans {
namespace {
using Json = nlohmann::json;
using services::Output;

const Json &field(const Json &object, const char *key) {
    static const Json missing;
    if (!object.is_object()) return missing;
    const auto found = object.find(key);
    return found == object.end() ? missing : *found;
}

const Json &rows(const Json &object, const char *key) {
    static const Json empty = Json::array();
    const auto &value = field(object, key);
    return value.is_array() ? value : empty;
}

std::string text(const Json &value) {
    if (const auto *string = value.get_ptr<const Json::string_t *>()) return *string;
    if (value.is_number_integer()) return value.dump();
    return {};
}

std::string text(const Json &object, const char *key) { return text(field(object, key)); }

std::int64_t number(const Json &object, const char *key) {
    const auto &value = field(object, key);
    if (value.is_number_unsigned()) return static_cast<std::int64_t>(std::min<std::uint64_t>(value.get<std::uint64_t>(), INT64_MAX));
    return value.is_number_integer() ? value.get<std::int64_t>() : 0;
}

bool flag(const Json &object, const char *key) { return field(object, key) == true; }

bool identifier(std::string_view value) {
    return !value.empty() && value.size() <= 80 && std::ranges::all_of(value, [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

std::string make_id() {
    static constexpr std::string_view hex = "0123456789abcdef";
    std::random_device source;
    std::string id;
    id.reserve(32);
    for (int word = 0; word < 4; ++word) {
        const auto random = static_cast<std::uint32_t>(source());
        for (unsigned nibble = 0; nibble < 8; ++nibble) id += hex[(random >> (nibble * 4)) & 15u];
    }
    return id;
}

bool has_step(const Json &steps, std::string_view id) {
    return std::ranges::any_of(steps, [id](const auto &step) {
        const auto *value = step.template get_ptr<const Json::string_t *>();
        return value && *value == id;
    });
}

std::string best_source(const Json &row) {
    for (const auto &source : rows(row, "sources")) {
        if (auto details = text(source, "details"); !details.empty()) return details;
        if (auto label = text(source, "label"); !label.empty()) return label;
    }
    return text(row, "sourceKind");
}

const Json &find_item(const Json &computed, std::string_view item) {
    static const Json missing;
    for (const auto &row : rows(computed, "bom")) if (text(row, "itemKey") == item) return row;
    return missing;
}

void recipe_fields(const Output &out, const Json &computed) {
    out.string("recipe_id", text(computed, "recipeId"));
    out.string("name", text(computed, "recipeName"));
    out.string("skill", text(computed, "tradeskill"));
    out.string("station", text(computed, "station"));
    out.number("trivial", number(computed, "trivial"));
    out.number("quantity", number(computed, "quantity"));
}

void preview_fields(const Output &out, const Json &computed) {
    out.key("preview"); out.begin();
    recipe_fields(out, computed);
    out.string("target_item_key", text(computed, "targetItemKey"));
    out.number("combines", number(computed, "totalCombines"));
    const auto output = number(computed, "output");
    if (output > 0) out.number("output", output);
    out.array("products");
    for (const auto &product : rows(computed, "products")) {
        out.begin();
        out.string("item_key", text(product, "itemKey"));
        out.string("name", text(product, "name"));
        out.number("quantity", number(product, "count"));
        out.end();
    }
    out.end();
    out.array("materials");
    for (const auto &row : rows(computed, "bom")) {
        if (number(row, "remaining") <= 0 || number(row, "recipeId") > 0 || flag(row, "worldObject")) continue;
        out.begin();
        out.string("name", text(row, "name"));
        out.number("quantity", number(row, "remaining"));
        out.end();
    }
    out.end(); out.end();
}

void plan_fields(const Output &out, const char *key, const Json &record) {
    const auto &plan = field(record, "plan");
    const auto &computed = field(record, "computed");
    const auto &checklist = rows(computed, "checklist");
    const auto &effective = field(computed, "effectiveCompleted");
    const auto &completed = effective.is_array() ? effective : rows(plan, "completed");
    const auto complete = std::ranges::count_if(checklist, [&](const auto &step) { return has_step(completed, text(step, "id")); });
    out.key(key); out.begin();
    recipe_fields(out, computed);
    out.string("id", text(plan, "id"));
    out.string("revision", text(record, "revision"));
    out.number("completed", complete);
    out.number("total", static_cast<std::int64_t>(checklist.size()));
    for (const auto &warning : rows(computed, "warnings")) {
        if (text(warning, "code") == "review-masked-items") {
            out.string("warning", text(warning, "message"));
            break;
        }
    }

    out.array("materials");
    for (const auto &row : rows(computed, "bom")) {
        if (flag(row, "worldObject") || number(row, "recipeId") > 0) continue;
        const auto item = text(row, "itemKey");
        const auto id = "acquire:" + item;
        out.begin();
        out.string("id", id);
        out.string("item_key", item);
        out.string("name", text(row, "name"));
        out.string("source", best_source(row));
        out.number("item_id", number(row, "itemId"));
        out.number("quantity", number(row, "need"));
        out.number("owned", number(row, "owned"));
        out.boolean("completed", has_step(completed, id));
        out.boolean("checkable", std::ranges::any_of(checklist, [&](const auto &step) { return text(step, "id") == id; }));
        out.end();
    }
    out.end();

    out.array("combines");
    for (const auto &step : checklist) {
        if (text(step, "type") == "acquire") continue;
        const auto id = text(step, "id");
        auto label = text(step, "label");
        if (label.starts_with("Make ")) label.erase(0, 5);
        out.begin();
        out.string("id", id);
        out.string("name", label);
        out.string("station", text(step, "description"));
        out.number("quantity", number(step, "quantity"));
        out.boolean("completed", has_step(completed, id));
        out.end();
    }
    out.end();

    out.array("choices");
    const auto &choices = field(computed, "alternatives");
    if (choices.is_object()) for (const auto &[item, options] : choices.items()) {
        if (!options.is_array() || options.empty()) continue;
        const auto &row = find_item(computed, item);
        if (row.is_null()) continue;
        auto selected = text(field(plan, "choices"), item.c_str());
        if (selected.empty()) selected = "auto";
        out.begin();
        out.string("item_key", item);
        out.string("name", text(row, "name"));
        out.string("selected", selected);
        out.array("options");
        out.begin(); out.string("id", "auto"); out.string("name", "Automatic"); out.end();
        for (const auto &option : options) {
            out.begin();
            out.string("id", text(option, "recipeId"));
            out.string("name", text(option, "name"));
            out.end();
        }
        out.begin(); out.string("id", "acquire"); out.string("name", "Acquire the finished item"); out.end();
        out.end(); out.end();
    }
    out.end(); out.end();
}
} // namespace

void Adapter::reset_context() {
    context_.reset();
    connected_ = catalog_more_ = retry_after_list_ = list_more_ = false;
    pin_checked_ = load_pinned_ = false;
    pending_ = 0;
    sent_at_ = 0;
    list_page_ = 1;
    deferred_.reset();
    reply_ = {};
    scope_.clear();
    message_.clear();
    plans_ = catalog_ = Json::array();
    record_ = preview_ = pinned_record_ = nullptr;
    revision_ = services::token();
}

void Adapter::set_host(const PluginHostApi *host) {
    if (HAILSTONE_HOST_HAS(host_, remove_ui_function)) host_->remove_ui_function(host_->host, "tradeskill_plans");
    host_ = host;
    peer_ = in_game_ = shown_ = false;
    pinned_scope_.clear();
    pinned_id_.clear();
    reset_context();
}

void Adapter::initialize() {
    if (HAILSTONE_HOST_HAS(host_, add_ui_function)) host_->add_ui_function(host_->host, "tradeskill_plans", callback, this);
}

void Adapter::peer(bool enabled) {
    if (peer_ != enabled) reset_context();
    peer_ = enabled;
    if (!enabled) shown_ = false;
}

void Adapter::game_state(int state) {
    in_game_ = state == GAMESTATE_INGAME;
    if (!in_game_ && context_) reset_context();
}

void Adapter::begin_zone() {
    in_game_ = false;
    reset_context();
}

bool Adapter::synchronize() {
    const auto current = services::context(host_, peer_, in_game_);
    if (current != context_) {
        reset_context();
        context_ = current;
    }
    return current.has_value();
}

void Adapter::pulse() {
    if (!synchronize()) return;
    const auto now = host_->monotonic_ms(host_->host);
    if (pending_ && (now < sent_at_ || now - sent_at_ > 15000)) {
        pending_ = 0;
        reply_ = {};
        message_ = request_ == Request::Write ? "The save is queued locally. Retry to confirm it." : "No reply received. Refresh when the server is ready.";
        revision_ = services::token();
    }
    if (deferred_ && !pending_ && (now < sent_at_ || now - sent_at_ >= 275)) {
        auto [request, body] = std::move(*deferred_);
        deferred_.reset();
        send(request, body);
    }
    if (list_more_ && !busy()) {
        list_more_ = false;
        send(Request::List, {{"action", "list"}, {"page", ++list_page_}});
    }
    if (retry_after_list_ && !busy()) {
        retry_after_list_ = false;
        retry();
    }
    if (load_pinned_ && !busy()) {
        load_pinned_ = false;
        if (scope_ == pinned_scope_ && !pinned_id_.empty()) send(Request::Pinned, {{"action", "get"}, {"id", pinned_id_}});
    }
    if (!pin_checked_ && !pinned_id_.empty() && !busy() && !connected_) {
        pin_checked_ = true;
        list_page_ = 1;
        send(Request::List, {{"action", "list"}});
    }
}

bool Adapter::send(Request request, const Json &body) {
    if (!synchronize() || busy()) return false;
    const auto now = host_->monotonic_ms(host_->host);
    if (sent_at_ && now >= sent_at_ && now - sent_at_ < 275) {
        deferred_.emplace(request, body);
        revision_ = services::token();
        return true;
    }
    const auto encoded = body.dump();
    const auto id = services::request_id();
    if (!id || encoded.empty() || encoded.size() > kMaxBody) return false;
    pending_ = id;
    request_ = request;
    sent_at_ = now;
    message_.clear();
    reply_ = {};
    revision_ = services::token();
    for (std::size_t offset = 0; offset < encoded.size(); offset += kFragment) {
        const auto packet = encode(id, 0, encoded, offset);
        if (!host_->send_packet(host_->host, HAILSTONE_STREAM_ZONE, kOpcode, packet.data(), packet.size())) {
            if (pending_ == id) pending_ = 0;
            message_ = request == Request::Write ? "The save is queued locally. Retry when connected." : "The game connection is unavailable.";
            revision_ = services::token();
            return false;
        }
    }
    return true;
}

bool Adapter::open() {
    if (!peer_) return false;
    shown_ = true;
    revision_ = services::token();
    if (!busy()) {
        list_page_ = 1;
        send(Request::List, {{"action", "list"}});
    }
    return true;
}

std::filesystem::path Adapter::outbox_path() const {
    if (!context_ || !identifier(scope_) || !HAILSTONE_HOST_HAS(host_, character_config_dir)) return {};
    std::array<char, 4096> path{};
    const auto length = host_->character_config_dir(host_->host, path.data(), path.size());
    if (length <= 0 || static_cast<std::size_t>(length) >= path.size() || !path.front()) return {};
    const std::u8string_view utf8(reinterpret_cast<const char8_t *>(path.data()), static_cast<std::size_t>(length));
    return std::filesystem::path(utf8) / ("tradeskill-pending-" + scope_ + ".json");
}

bool Adapter::queued() const {
    const auto path = outbox_path();
    std::error_code error;
    return !path.empty() && std::filesystem::exists(path, error);
}

bool Adapter::clear_write() {
    const auto path = outbox_path();
    if (path.empty()) return false;
    std::error_code error;
    std::filesystem::remove(path, error);
    return !error;
}

bool Adapter::retry() {
    if (!synchronize() || !connected_ || busy()) return false;
    const auto path = outbox_path();
    if (path.empty()) return false;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > kMaxBody) return false;
    std::string bytes(static_cast<std::size_t>(size), '\0');
    std::ifstream input(path, std::ios::binary);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return false;
    auto body = Json::parse(bytes, nullptr, false);
    const auto action = text(body, "action");
    return (action == "put" || action == "create") && send(Request::Write, body);
}

bool Adapter::write(const Json &body) {
    if (!synchronize() || !connected_ || busy() || queued()) return false;
    const auto path = outbox_path();
    const auto encoded = body.dump();
    if (path.empty() || encoded.size() > kMaxBody) return false;
    auto staged = path;
    staged += ".tmp";
    {
        std::ofstream output(staged, std::ios::binary | std::ios::trunc);
        output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        output.close();
        if (!output) {
            message_ = "The pending save could not be written locally.";
            revision_ = services::token();
            return false;
        }
    }
    std::error_code error;
    std::filesystem::rename(staged, path, error);
    if (error) {
        message_ = "The pending save could not be stored locally.";
        revision_ = services::token();
        return false;
    }
    return send(Request::Write, body);
}

bool Adapter::packet(std::uint16_t opcode, const void *data, std::size_t size) {
    if (opcode != kOpcode) return false;
    if (!synchronize() || !pending_) return true;
    const auto chunk = decode(data, size);
    if (!chunk || chunk->request != pending_ || chunk->status < 100 || chunk->status > 599 || !reply_.append(*chunk)) return true;
    if (!reply_.complete()) return true;
    auto reply = std::move(reply_);
    reply_ = {};
    pending_ = 0;
    receive(request_, reply.status, Json::parse(reply.body, nullptr, false));
    revision_ = services::token();
    return true;
}

void Adapter::set_record(Json record, bool keep_selection) {
    const auto id = text(field(record, "plan"), "id");
    if (!identifier(id)) return;
    const bool archived = flag(record, "archived");
    const auto existing = std::ranges::find_if(plans_, [&](const auto &entry) { return text(field(entry, "plan"), "id") == id; });
    if (archived) {
        if (existing != plans_.end()) plans_.erase(existing);
        if (id == pinned_id_) {
            pinned_id_.clear();
            pinned_scope_.clear();
            pinned_record_ = nullptr;
        }
        if (text(field(record_, "plan"), "id") == id) record_ = nullptr;
        return;
    }
    auto summary = record;
    summary.erase("computed");
    if (existing == plans_.end()) plans_.push_back(std::move(summary));
    else *existing = std::move(summary);
    if (id == pinned_id_ && scope_ == pinned_scope_) pinned_record_ = record;
    if (!keep_selection || text(field(record_, "plan"), "id") == id) record_ = std::move(record);
}

void Adapter::receive(Request request, std::uint16_t status, Json body) {
    auto message = text(body, "message");
    if (message.empty()) message = text(body, "error");
    if (status == 409) {
        if (field(body, "record").is_object()) set_record(std::move(body["record"]), true);
        message_ = "This plan changed elsewhere. The current plan has been loaded and your attempted edit is still queued locally.";
        return;
    }
    if (status < 200 || status >= 300 || !body.is_object()) {
        if (status == 403 || status == 503) connected_ = false;
        message_ = !message.empty() ? std::move(message) : "Bastion could not load the requested plan data.";
        return;
    }
    connected_ = true;
    if (const auto scope = text(body, "scope"); identifier(scope) && scope != scope_) {
        if (!scope_.empty()) {
            plans_ = catalog_ = Json::array();
            record_ = preview_ = pinned_record_ = nullptr;
        }
        scope_ = scope;
        if (scope_ != pinned_scope_) {
            pinned_id_.clear();
            pinned_scope_.clear();
        }
    }
    if (request == Request::List) {
        if (list_page_ == 1) plans_ = Json::array();
        for (const auto &entry : rows(body, "plans")) if (!flag(entry, "archived")) plans_.push_back(entry);
        list_more_ = flag(body, "hasMore") && list_page_ < 20;
        retry_after_list_ = !list_more_ && queued();
        load_pinned_ = !list_more_ && !pinned_id_.empty() && scope_ == pinned_scope_;
    } else if (request == Request::Catalog) {
        catalog_ = rows(body, "recipes");
        catalog_more_ = flag(body, "hasMore");
    } else if (request == Request::Preview) {
        preview_ = field(body, "computed");
    } else if (field(body, "record").is_object()) {
        const auto id = text(field(body["record"], "plan"), "id");
        if (request == Request::Pinned && id != pinned_id_) return;
        set_record(std::move(body["record"]), request == Request::Pinned ||
            (request == Request::Write && id == pinned_id_));
        if (request == Request::Write) {
            preview_ = nullptr;
            message_ = clear_write() ? "Saved to your Bastion account." : "Saved to Bastion. The local retry copy could not be removed.";
        }
    }
}

bool Adapter::update(const HailstoneArgs *args) {
    const auto id = services::argument(host_, args, 1, 80);
    const auto revision = services::argument(host_, args, 2, 24);
    const auto change = services::argument(host_, args, 3, 16);
    const auto key = services::argument(host_, args, 4, 140);
    const auto value = services::argument(host_, args, 5, 32);
    if (!id) return false;
    const auto &record = *id == text(field(record_, "plan"), "id") ? record_ : pinned_record_;
    const auto &current = field(record, "plan");
    const auto &computed = field(record, "computed");
    if (!id || !revision || !change || !key || !value || !identifier(*id) || *id != text(current, "id") ||
        *revision != text(record, "revision")) return false;
    const auto digits = services::decimal(host_, args, 5);
    auto plan = current;
    bool archived = false;
    if (*change == "quantity" && digits && *digits > 0 && *digits <= 10000) {
        plan["quantity"] = *digits;
    } else if (*change == "inventory" && digits && *digits <= 1000000) {
        const auto item = key->starts_with("acquire:") ? key->substr(8) : *key;
        if (item.empty() || find_item(computed, item).is_null()) return false;
        if (!plan["inventory"].is_object()) plan["inventory"] = Json::object();
        if (*digits) plan["inventory"][item] = *digits;
        else plan["inventory"].erase(item);
    } else if (*change == "choice") {
        const auto &options = rows(field(computed, "alternatives"), key->c_str());
        if (options.empty()) return false;
        auto &choices = plan["choices"];
        if (!choices.is_object()) choices = Json::object();
        if (*value == "auto") choices.erase(*key);
        else if (*value == "acquire") choices[*key] = *value;
        else if (digits && std::ranges::any_of(options, [&](const auto &option) { return text(option, "recipeId") == *value; }))
            choices[*key] = *digits;
        else return false;
    } else if (*change == "step" && (*value == "1" || *value == "0")) {
        const bool complete = *value == "1";
        if (complete && !std::ranges::any_of(rows(computed, "checklist"), [&](const auto &step) { return text(step, "id") == *key; })) return false;
        auto &steps = plan["completed"];
        if (!steps.is_array()) steps = Json::array();
        std::erase_if(steps.get_ref<Json::array_t &>(), [&](const auto &step) { return text(step) == *key; });
        if (complete) steps.push_back(*key);
    } else if (*change == "archive" && *value == "1") {
        archived = true;
    } else {
        return false;
    }
    return write({{"action", "put"}, {"id", *id}, {"baseRevision", field(record, "revision")},
                  {"plan", std::move(plan)}, {"archived", archived}});
}

void Adapter::publish(const Output &out) const {
    out.boolean("available", connected_);
    out.boolean("fresh", connected_ && !busy());
    out.boolean("pending", busy());
    out.boolean("queued", queued());
    out.boolean("catalog_more", catalog_more_);
    out.string("message", message_);
    out.identity("revision", revision_);

    out.array("plans");
    for (const auto &entry : plans_) {
        const auto &plan = field(entry, "plan");
        out.begin();
        out.string("id", text(plan, "id"));
        out.string("name", text(plan, "recipeName"));
        out.number("quantity", number(plan, "quantity"));
        out.number("completed", field(plan, "completedCount").is_number_integer() ? number(plan, "completedCount") :
            static_cast<std::int64_t>(rows(plan, "completed").size()));
        out.number("total", number(plan, "stepCount"));
        out.end();
    }
    out.end();

    out.array("catalog");
    for (const auto &recipe : catalog_) {
        out.begin();
        out.string("id", text(recipe, "id"));
        out.string("name", text(recipe, "name"));
        out.string("skill", text(recipe, "tradeskill"));
        out.string("station", text(recipe, "station"));
        out.number("trivial", number(recipe, "trivial"));
        out.end();
    }
    out.end();

    if (preview_.is_object()) preview_fields(out, preview_);
    if (record_.is_object()) plan_fields(out, "plan", record_);
    if (pinned_record_.is_object()) plan_fields(out, "pinned_plan", pinned_record_);
}

int Adapter::callback(void *user, const HailstoneArgs *args, HailstoneOut *out) noexcept {
    auto &adapter = *static_cast<Adapter *>(user);
    return services::guarded_ui(adapter.host_, [&] { return adapter.call(args, out); });
}

int Adapter::call(const HailstoneArgs *args, HailstoneOut *result) {
    const auto action = services::argument(host_, args, 0, 24);
    const Output out{host_, result};
    if (!action || !out.valid()) return 0;
    synchronize();
    out.begin();
    if (*action == "visibility") {
        out.boolean("active", peer_ && in_game_);
        out.boolean("shown", shown_);
        out.string("scope", scope_);
        out.boolean("pending", busy());
        out.string("message", message_);
        out.identity("revision", revision_);
    } else if (*action == "state") {
        publish(out);
    } else {
        bool accepted = false;
        if (*action == "open") {
            const auto id = services::argument(host_, args, 1, 80);
            accepted = !id ? open() : identifier(*id) && send(Request::Plan, {{"action", "get"}, {"id", *id}});
            if (accepted) shown_ = true;
        } else if (*action == "close") {
            shown_ = false;
            revision_ = services::token();
            accepted = true;
        } else if (*action == "list" || *action == "refresh") {
            if (!busy()) {
                list_page_ = 1;
                list_more_ = false;
                accepted = send(Request::List, {{"action", "list"}});
            }
        } else if (*action == "search") {
            const auto skill = services::argument(host_, args, 1, 48);
            const auto query = services::argument(host_, args, 2, 96);
            const auto page = services::decimal(host_, args, 3).value_or(1);
            accepted = skill && query && page > 0 && page <= 1000000 && send(Request::Catalog,
                {{"action", "catalog"}, {"skill", *skill}, {"query", *query}, {"page", page}});
        } else if (*action == "create" || *action == "preview") {
            const auto recipe = services::decimal(host_, args, 1);
            const auto quantity = services::decimal(host_, args, 2);
            const auto target = services::argument(host_, args, 3, 96);
            if (recipe && *recipe <= UINT32_MAX && *recipe && quantity && *quantity && *quantity <= 10000) {
                Json draft{{"recipeId", *recipe}, {"quantity", *quantity}};
                if (target && !target->empty()) draft["targetItemKey"] = *target;
                if (*action == "create") {
                    draft["action"] = "create";
                    draft["id"] = make_id();
                    accepted = write(draft);
                } else {
                    accepted = send(Request::Preview, {{"action", "preview"}, {"plan", std::move(draft)}});
                }
            }
        } else if (*action == "update") {
            accepted = update(args);
        } else if (*action == "pin") {
            const auto id = services::argument(host_, args, 1, 80);
            if (id && identifier(*id) && !scope_.empty() && *id == text(field(record_, "plan"), "id")) {
                pinned_id_ = *id;
                pinned_scope_ = scope_;
                pinned_record_ = record_;
                pin_checked_ = true;
                revision_ = services::token();
                accepted = true;
            }
        } else if (*action == "unpin") {
            pinned_id_.clear();
            pinned_scope_.clear();
            pinned_record_ = nullptr;
            revision_ = services::token();
            accepted = true;
        } else if (*action == "retry") {
            accepted = retry();
        } else if (*action == "discard_pending" && !busy()) {
            accepted = clear_write();
            if (accepted) {
                message_ = "The pending local edit was discarded.";
                revision_ = services::token();
            }
        }
        out.boolean("accepted", accepted);
    }
    out.end();
    return 1;
}
} // namespace bastion::tradeskill_plans
