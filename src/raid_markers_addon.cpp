#include "raid_markers_addon.h"
#include "citadel_services.h"
#include "raid_marker_render.h"
#include "raid_snapshot.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace bastion::raid_markers {
namespace {
constexpr std::uint16_t kRaidUpdate = 0x3973;
constexpr std::uint16_t kSpecialMessage = 0x0083;
constexpr std::uint64_t kSendInterval = 3200;
constexpr std::uint64_t kSnapshotInterval = 12000;
constexpr std::uint64_t kSessionTimeout = 42000;
constexpr std::uint64_t kQueryInterval = 2000;
constexpr std::uint64_t kQueryTimeout = 6000;
constexpr std::uint64_t kRosterRebuildTime = 1000;
constexpr std::size_t kMaxPending = 16;
constexpr auto kRequiredCaps = HAILSTONE_CAP_READ | HAILSTONE_CAP_UI |
    HAILSTONE_CAP_EXTEND | HAILSTONE_CAP_PACKETS;

std::uint64_t new_nonce() {
    std::random_device random;
    const auto value = (static_cast<std::uint64_t>(random()) << 32) | random();
    return value ? value : services::token();
}

bool character_name(std::string_view name) {
    return !name.empty() && name.size() < 64 && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    });
}

std::optional<std::string> server_text(std::span<const std::byte> bytes) {
    // Bastion's #raidmanager reply is OP_SpecialMesg, raw mode and White (10),
    // with an empty speaker, zero target and zero location.
    if (bytes.size() < 25 || bytes.size() > 4096) return std::nullopt;
    try {
        hailstone::ByteReader reader(bytes);
        if (reader.le<std::uint8_t>() != 0 || reader.le<std::uint8_t>() != 0 ||
            reader.le<std::uint8_t>() != 0 || reader.le<std::uint32_t>() != 10 ||
            reader.le<std::uint32_t>() != 0 || reader.le<std::uint8_t>() != 0 ||
            reader.le<std::uint32_t>() != 0 || reader.le<std::uint32_t>() != 0 ||
            reader.le<std::uint32_t>() != 0)
            return std::nullopt;
        auto message = reader.cstring();
        if (!reader.empty()) return std::nullopt;
        return message;
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}
} // namespace

void Adapter::set_host(const PluginHostApi* host) {
    if (HAILSTONE_HOST_HAS(host_, remove_ui_function))
        host_->remove_ui_function(host_->host, "raid_markers");
    reset_raid();
    host_ = host;
    selected_ = in_zone_ = world_zoning_ = false;
    next_send_ = 0;
}

void Adapter::initialize() {
    if (HAILSTONE_HOST_HAS(host_, add_ui_function))
        host_->add_ui_function(host_->host, "raid_markers", callback, this);
}

void Adapter::peer(bool selected) {
    selected_ = selected;
    if (!selected) begin_zone();
}

void Adapter::game_state(int state) {
    if (state == GAMESTATE_INGAME) {
        in_zone_ = true;
        world_zoning_ = false;
        return;
    }
    begin_zone();
    if (state == GAMESTATE_CHARSELECT || state == GAMESTATE_UNLOADING ||
        (state == GAMESTATE_PRECHARSELECT && !world_zoning_))
        reset_raid();
}

void Adapter::world_login(bool zoning) {
    world_zoning_ = zoning;
    begin_zone();
    if (!zoning) reset_raid();
}

void Adapter::begin_zone() {
    cancel_aim();
    in_zone_ = false;
    placement_ = {};
    outgoing_.clear();
    manager_requests_.clear();
    query_pending_ = false;
    sync_needed_ = true;
}

bool Adapter::allowed() const {
    return selected_ && in_zone_ && HAILSTONE_HOST_HAS(host_, caps) &&
        (host_->caps(host_->host) & kRequiredCaps) == kRequiredCaps &&
        HAILSTONE_HOST_HAS(host_, get_player) && HAILSTONE_HOST_HAS(host_, get_raid_data_bulk) &&
        HAILSTONE_HOST_HAS(host_, monotonic_ms) && HAILSTONE_HOST_HAS(host_, send_packet) &&
        HAILSTONE_HOST_HAS(host_, world_capabilities) &&
        (host_->world_capabilities(host_->host) & HAILSTONE_WORLD_PLACEMENT) != 0 &&
        HAILSTONE_HOST_HAS(host_, world_placement_start) && HAILSTONE_HOST_HAS(host_, world_placement_get) &&
        HAILSTONE_HOST_HAS(host_, world_placement_cancel) && HAILSTONE_HOST_HAS(host_, world_submit);
}

void Adapter::cancel_aim() {
    if (aiming_ && HAILSTONE_HOST_HAS(host_, world_placement_cancel))
        host_->world_placement_cancel(host_->host);
    aiming_ = 0;
}

void Adapter::reset_session() {
    cancel_aim();
    slots_ = {};
    manager_requests_.clear();
    outgoing_.clear();
    hosting_ = dirty_ = query_pending_ = false;
    retired_nonce_ = nonce_;
    nonce_ = revision_ = last_snapshot_ = next_query_ = 0;
    error_.clear();
}

void Adapter::reset_raid() {
    reset_session();
    actual_leader_.clear();
    effective_leader_.clear();
    roster_.clear();
    player_.clear();
    check_leader_ = false;
    sync_needed_ = false;
    roster_rebuild_until_ = 0;
}

const HS_BulkRaidData* Adapter::member(std::string_view name) const {
    const auto it = std::find_if(roster_.begin(), roster_.end(), [&](const auto& row) {
        return raid::same_name(services::bounded(row.name), name);
    });
    return it == roster_.end() ? nullptr : &*it;
}

bool Adapter::refresh_roster() {
    if (!allowed()) return false;
    HailstonePlayer player{};
    player.size = sizeof(player);
    if (!host_->get_player(host_->host, &player) || !player.in_zone) return false;
    auto snapshot = raid::read(host_);
    if (!snapshot) return false;
    if (!snapshot->header.active) {
        if (host_->monotonic_ms(host_->host) >= roster_rebuild_until_) reset_raid();
        return false;
    }

    const auto name = services::bounded(player.name);
    if (!character_name(name)) return false;
    if (!player_.empty() && !raid::same_name(player_, name)) reset_raid();
    player_ = name;
    const auto leader_index = snapshot->header.leaderIndex;
    if (leader_index >= snapshot->members.size()) return false;
    const auto leader = services::bounded(snapshot->members[leader_index].name);
    if (!character_name(leader)) return false;
    if (hosting_ && roster_.size() != snapshot->members.size()) dirty_ = true;
    roster_ = std::move(snapshot->members);
    if (!member(player_)) return false;
    roster_rebuild_until_ = 0;
    if (!raid::same_name(effective_leader_, leader)) {
        effective_leader_ = leader;
        if (raid::same_name(leader, player_)) {
            check_leader_ = true;
        } else if (!raid::same_name(actual_leader_, leader) || hosting_) {
            reset_session();
            actual_leader_ = leader;
            sync_needed_ = true;
        }
    }
    return true;
}

void Adapter::observe_raid(std::span<const std::byte> bytes) {
    if (bytes.size() < 136) return;
    hailstone::ByteReader reader(bytes);
    const auto action = reader.le<std::uint32_t>();
    if (action == 5) {
        // Bastion also sends a targeted Disband immediately before rebuilding
        // the client's roster after some subgroup moves. Keep the slots only
        // if the same raid resumes promptly.
        const auto name = reader.fixed_string(64);
        if (raid::same_name(name, player_) && !actual_leader_.empty() &&
            HAILSTONE_HOST_HAS(host_, monotonic_ms)) {
            roster_rebuild_until_ = host_->monotonic_ms(host_->host) + kRosterRebuildTime;
            roster_.clear();
            cancel_aim();
            placement_ = {};
            outgoing_.clear();
            manager_requests_.clear();
            query_pending_ = false;
        } else {
            reset_raid();
        }
        return;
    }
    if (action == 10) { // No raid.
        reset_raid();
        return;
    }
    if (action != 8 && action != 30) return;
    reader.skip(68);
    const auto leader = reader.fixed_string(64);
    if (!character_name(leader)) return;
    if (action == 8 && !raid::same_name(actual_leader_, leader)) {
        // The RoF2 raid-create packet names the actual leader. The later stock
        // leadership update may name the local character when it is a manager.
        reset_session();
        actual_leader_ = leader;
        sync_needed_ = true;
    }
    if (action == 8 || raid::same_name(leader, player_)) check_leader_ = true;
}

bool Adapter::has_marker_role() const {
    const auto* self = player_.empty() ? nullptr : member(player_);
    return self && (self->flags & (HS_BULK_RAID_LEADER | HS_BULK_RAID_MAIN_ASSIST));
}

bool Adapter::can_place() const {
    if (!has_marker_role() || !nonce_ || actual_leader_.empty()) return false;
    if (raid::same_name(effective_leader_, player_)) return true;
    const auto* leader = member(actual_leader_);
    return leader && (leader->flags & HS_BULK_RAID_PRESENT);
}

void Adapter::query_managers(std::uint64_t now) {
    if (query_pending_ || now < next_query_ || !HAILSTONE_HOST_HAS(host_, do_command) ||
        !(host_->caps(host_->host) & HAILSTONE_CAP_CHAT))
        return;
    query_pending_ = true;
    query_until_ = now + kQueryTimeout;
    next_query_ = now + kQueryInterval;
    host_->do_command(host_->host, "/say #raidmanager list");
}

bool Adapter::manager_list(std::span<const std::byte> bytes) {
    if (!query_pending_ || host_->monotonic_ms(host_->host) >= query_until_) return false;
    const auto text = server_text(bytes);
    if (!text) return false;
    if (*text == "Only the raid leader can add or remove raid managers." ||
        *text == "You must be in a raid to use this command.") {
        const bool was_hosting = hosting_;
        if (was_hosting) reset_session();
        query_pending_ = check_leader_ = false;
        manager_requests_.clear();
        return true;
    }

    constexpr std::string_view prefix = "Raid managers: ";
    std::vector<std::string> names;
    if (*text != "This raid has no raid managers.") {
        if (!text->starts_with(prefix)) return false;
        std::string_view list(*text);
        list.remove_prefix(prefix.size());
        for (;;) {
            const auto comma = list.find(", ");
            const auto name = list.substr(0, comma);
            if (!character_name(name) || names.size() >= raid::MaxMembers) return false;
            names.emplace_back(name);
            if (comma == std::string_view::npos) break;
            list.remove_prefix(comma + 2);
        }
    }
    query_pending_ = check_leader_ = false;
    if (!refresh_roster() || !raid::same_name(effective_leader_, player_)) return true;
    if (!hosting_) {
        reset_session();
        actual_leader_ = player_;
        nonce_ = new_nonce();
        revision_ = 1;
        hosting_ = dirty_ = true;
    }
    for (const auto& request : manager_requests_) {
        if (request.message.nonce == nonce_ && member(request.sender) &&
            std::any_of(names.begin(), names.end(), [&](const auto& name) {
                return raid::same_name(request.sender, name);
            }))
            apply(request.message);
    }
    manager_requests_.clear();
    return true;
}

void Adapter::apply(const Message& message) {
    switch (message.kind) {
        case Kind::Set: {
            const auto& marker = message.markers.front();
            slots_[marker.id - 1] = marker;
            break;
        }
        case Kind::Clear:
            slots_[message.marker_id - 1].reset();
            break;
        case Kind::ClearAll:
            slots_ = {};
            break;
        default:
            return;
    }
    ++revision_;
    dirty_ = true;
}

bool Adapter::enqueue(Message message) {
    if (!can_place()) return false;
    message.nonce = nonce_;
    if (hosting_) {
        apply(message);
        return true;
    }
    if (outgoing_.size() >= kMaxPending) return false;
    outgoing_.push_back(std::move(message));
    return true;
}

void Adapter::receive(const Received& incoming, std::uint64_t now) {
    if (!refresh_roster()) return;
    const auto* sender = member(incoming.sender);
    if (!sender || raid::same_name(incoming.sender, player_)) return;
    const auto& message = incoming.message;
    if (message.kind == Kind::Snapshot) {
        // Only the real leader can originate raid-scoped Sentinel traffic.
        // Ordinary members also know the true leader from their roster. A raid
        // manager gets its own name there, so use the raid-scoped sender.
        if (hosting_ || (!raid::same_name(effective_leader_, player_) &&
                         !raid::same_name(effective_leader_, incoming.sender)))
            return;
        if (message.nonce == retired_nonce_ || (message.nonce == nonce_ && message.revision < revision_))
            return;
        if (nonce_ != message.nonce) {
            reset_session();
            nonce_ = message.nonce;
        }
        actual_leader_ = incoming.sender;
        if (message.revision > revision_) {
            slots_ = {};
            for (const auto& marker : message.markers) slots_[marker.id - 1] = marker;
            revision_ = message.revision;
        }
        last_snapshot_ = now;
        sync_needed_ = false;
        return;
    }

    if (!hosting_ || !raid::same_name(effective_leader_, player_)) return;
    if (message.kind == Kind::RequestSync) {
        dirty_ = true;
        return;
    }
    if (message.nonce != nonce_) return;
    // Bastion sends main-assist role changes within the zone. Require the
    // assist to be locally present so an off-zone cached role cannot authorize
    // a marker edit after revocation.
    constexpr auto local_assist = HS_BULK_RAID_MAIN_ASSIST | HS_BULK_RAID_PRESENT;
    const bool same_instance = message.kind != Kind::Set ||
        (message.markers.front().zone_id == placement_.zone_id &&
         message.markers.front().instance_id == placement_.instance_id);
    if ((sender->flags & local_assist) == local_assist && same_instance) {
        apply(message);
    } else if (manager_requests_.size() < kMaxPending) {
        // Managers are not visible in the leader's RoF2 roster. Authorize
        // these edits only after the leader receives a fresh server response.
        manager_requests_.push_back(incoming);
        query_managers(now);
    }
}

bool Adapter::packet(std::uint16_t opcode, const void* data, std::size_t size) {
    if (!selected_ || !data) return false;
    const std::span bytes(static_cast<const std::byte*>(data), size);
    if (opcode == kRaidUpdate) {
        observe_raid(bytes);
        return false;
    }
    if (!allowed()) return false;
    if (opcode == kSpecialMessage) return manager_list(bytes);
    if (opcode != kOpcode) return false;
    Received incoming;
    const auto result = decode(bytes, incoming);
    if (result == DecodeResult::Valid)
        receive(incoming, host_->monotonic_ms(host_->host));
    return result != DecodeResult::Unrelated;
}

bool Adapter::send(const Message& message, std::string_view recipient, std::uint64_t now) {
    const auto bytes = encode(message, services::request_id(), recipient);
    if (!bytes) return false;
    next_send_ = now + kSendInterval;
    return host_->send_packet(host_->host, HAILSTONE_STREAM_ZONE, kOpcode, bytes->data(), bytes->size()) != 0;
}

void Adapter::sync(std::uint64_t now) {
    if (now < next_send_) return;
    if (hosting_) {
        if (!dirty_ && now - last_snapshot_ < kSnapshotInterval) return;
        Message snapshot{Kind::Snapshot, nonce_, revision_, 0, {}};
        for (const auto& slot : slots_)
            if (slot) snapshot.markers.push_back(*slot);
        if (send(snapshot, {}, now)) {
            dirty_ = false;
            last_snapshot_ = now;
        }
    } else if (!outgoing_.empty() && can_place()) {
        if (send(outgoing_.front(), actual_leader_, now)) outgoing_.pop_front();
    } else if (sync_needed_ && nonce_ && !actual_leader_.empty()) {
        if (send(Message{}, actual_leader_, now)) sync_needed_ = false;
    }
}

void Adapter::pulse() {
    placement_ = {};
    if (!refresh_roster()) {
        cancel_aim();
        return;
    }
    const auto now = host_->monotonic_ms(host_->host);
    if (query_pending_ && now >= query_until_) {
        query_pending_ = false;
        manager_requests_.clear();
        check_leader_ = check_leader_ && raid::same_name(effective_leader_, player_);
        if (!hosting_ && check_leader_) next_query_ = now + kQueryInterval;
    }
    if (!hosting_ && nonce_ && now - last_snapshot_ > kSessionTimeout) {
        const auto retired = retired_nonce_;
        reset_session();
        retired_nonce_ = retired;
    }
    if ((check_leader_ && raid::same_name(effective_leader_, player_)) ||
        (hosting_ && !manager_requests_.empty()))
        query_managers(now);

    if (!can_place()) {
        cancel_aim();
        outgoing_.clear();
    }
    placement_.size = sizeof(placement_);
    if (host_->world_placement_get(host_->host, &placement_)) {
        if (aiming_ && placement_.status == HAILSTONE_WORLD_PLACEMENT_CONFIRMED) {
            const auto& p = placement_.position;
            Message request{Kind::Set, 0, 0, 0,
                {{aiming_, placement_.zone_id, placement_.instance_id, p[0], p[1], p[2]}}};
            aiming_ = 0;
            const bool point_valid = placement_.zone_id &&
                std::all_of(std::begin(p), std::end(p), [](float coordinate) {
                    return std::isfinite(coordinate) && std::abs(coordinate) <= kMaximumCoordinate;
                });
            if (!point_valid || !enqueue(std::move(request))) error_ = "Could not queue marker placement.";
        } else if (placement_.status == HAILSTONE_WORLD_PLACEMENT_CANCELLED ||
                   placement_.status == HAILSTONE_WORLD_PLACEMENT_INACTIVE) {
            aiming_ = 0;
        }
    } else {
        placement_ = {};
        cancel_aim();
    }
    sync(now);
}

void Adapter::world_frame(const HailstoneWorldFrame* frame) const {
    if (!frame || !allowed() || !nonce_ || !placement_.zone_id) return;
    std::array<VisualMarker, kMarkerCount> visuals{};
    std::size_t count = 0;
    for (const auto& slot : slots_) {
        if (slot && slot->zone_id == placement_.zone_id && slot->instance_id == placement_.instance_id)
            visuals[count++] = {slot->id, slot->x, slot->y, slot->z};
    }
    std::optional<VisualMarker> preview;
    if (aiming_ && placement_.status == HAILSTONE_WORLD_PLACEMENT_AIMING_VALID) {
        const auto& p = placement_.position;
        preview = VisualMarker{aiming_, p[0], p[1], p[2]};
    }
    draw_world(*host_, *frame, std::span(visuals).first(count), preview);
}

int Adapter::callback(void* user, const HailstoneArgs* args, HailstoneOut* out) noexcept {
    auto& self = *static_cast<Adapter*>(user);
    return services::guarded_ui(self.host_, [&] { return self.ui(args, out); });
}

int Adapter::ui(const HailstoneArgs* args, HailstoneOut* out) {
    const services::Output output{host_, out};
    if (!output.valid()) return 0;
    const auto op = services::argument(host_, args, 0, 16);
    if (!op) return 0;
    if (*op == "state") {
        const bool available = allowed() && !roster_.empty();
        const bool can_edit = available && can_place();
        output.begin();
        output.number("available", available);
        output.number("show_bar", available && has_marker_role());
        output.number("can_place", can_edit);
        output.number("aiming", aiming_);
        output.array("slots");
        for (const auto& slot : slots_) {
            HailstoneValue value{HAILSTONE_INT, slot ? 1 : 0, 0.0, nullptr};
            host_->out_value(host_->host, out, &value);
        }
        output.end();
        output.string("error", error_);
        if (!can_edit)
            output.string("status", nonce_ ? "Join the raid leader’s instance to place markers" : "Waiting for raid leader");
        output.end();
        return 1;
    }

    bool success = false;
    std::uint8_t id = 0;
    HailstoneValue index{};
    if ((*op == "aim" || *op == "clear") && host_->arg(host_->host, args, 1, &index) &&
        index.kind == HAILSTONE_INT && index.i >= 1 && index.i <= kMarkerCount)
        id = static_cast<std::uint8_t>(index.i);
    if (*op == "cancel") {
        cancel_aim();
        success = true;
    } else if (refresh_roster() && can_place()) {
        if (*op == "aim" && id) {
            cancel_aim();
            if ((success = host_->world_placement_start(host_->host) != 0)) aiming_ = id;
        } else if ((*op == "clear" && id) || *op == "clear_all") {
            cancel_aim();
            success = enqueue(Message{*op == "clear" ? Kind::Clear : Kind::ClearAll, 0, 0, id, {}});
        }
    }
    error_ = success ? "" : "Raid marker action unavailable.";
    output.begin();
    output.number("ok", success);
    output.string("error", error_);
    output.end();
    return 1;
}

} // namespace bastion::raid_markers
