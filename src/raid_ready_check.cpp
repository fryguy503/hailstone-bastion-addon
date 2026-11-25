// SPDX-License-Identifier: MIT
#include "raid_ready_check.h"

#include "raid_snapshot.h"

namespace bastion::ready_check {
namespace {
constexpr uint64_t kObserveInterval = 250;
constexpr uint64_t kHelloInterval = 10000;
constexpr uint64_t kInitialRaidRetry = 3000;
constexpr uint64_t kUnsupportedIdleRetry = 30000;
constexpr uint64_t kSnapshotFreshness = 25000;
constexpr uint64_t kActionInterval = 350;

constexpr const char* kPhaseNames[] = {"idle", "active", "ended", "expired"};
constexpr const char* kStatusNames[] = {"no_response", "ready", "not_ready", "legacy"};
constexpr const char* kCapabilityNames[] = {"unknown", "modern", "ready_check", "legacy"};
}  // namespace

void Adapter::set_host(const PluginHostApi* host)
{
    if (HAILSTONE_HOST_HAS(host_, remove_ui_function)) {
        host_->remove_ui_function(host_->host, "readycheck");
    }
    host_ = host;
    selected_ = in_game_ = false;
    reset();
}

void Adapter::initialize()
{
    if (HAILSTONE_HOST_HAS(host_, add_ui_function)) {
        host_->add_ui_function(host_->host, "readycheck", callback, this);
    }
}

void Adapter::peer(bool selected)
{
    selected_ = selected;
    reset();
}

void Adapter::game_state(int state)
{
    const bool in_game = state == GAMESTATE_INGAME;
    if (in_game_ != in_game) {
        in_game_ = in_game;
        reset();
    }
}

void Adapter::begin_zone()
{
    in_game_ = false;
    reset();
}

void Adapter::reset()
{
    context_.reset();
    snapshot_.reset();
    now_ = received_at_ = 0;
    next_observation_ = next_hello_ = next_action_ = 0;
}

void Adapter::observe(bool force)
{
    if (!selected_ || !in_game_ || !HAILSTONE_HOST_HAS(host_, get_player) ||
        !HAILSTONE_HOST_HAS(host_, send_packet) || !HAILSTONE_HOST_HAS(host_, monotonic_ms)) {
        return;
    }

    const auto now = host_->monotonic_ms(host_->host);
    if (now < now_) reset();
    now_ = now;
    if (!force && now_ < next_observation_) return;
    next_observation_ = now_ + kObserveInterval;

    std::optional<Context> current;
    HailstonePlayer player{};
    player.size = sizeof(player);
    if (host_->get_player(host_->host, &player) &&
        player.size >= offsetof(HailstonePlayer, in_zone) + sizeof(player.in_zone) &&
        player.in_zone && player.spawn_id != 0 && player.name[0] != '\0') {
        const auto instance = player.size >= offsetof(HailstonePlayer, zone_instance) + sizeof(player.zone_instance)
            ? player.zone_instance : 0;
        current = Context{player.spawn_id, player.zone_id, instance, services::bounded(player.name), player.in_raid != 0};
    }
    if (current != context_) {
        context_ = std::move(current);
        snapshot_.reset();
        received_at_ = next_hello_ = next_action_ = 0;
    }
}

bool Adapter::fresh() const
{
    return snapshot_ && now_ - received_at_ < kSnapshotFreshness;
}

uint32_t Adapter::remaining_ms() const
{
    if (!snapshot_) return 0;
    const auto elapsed = now_ - received_at_;
    return elapsed < snapshot_->remaining_ms ? snapshot_->remaining_ms - static_cast<uint32_t>(elapsed) : 0;
}

bool Adapter::send(Action action, uint64_t check_id, Status choice) const
{
    const auto bytes = MakeRequest(action, check_id, choice);
    return host_->send_packet(host_->host, HAILSTONE_STREAM_ZONE, kOpcode, bytes.data(), bytes.size()) != 0;
}

void Adapter::pulse()
{
    observe();
    if (!context_ || now_ < next_hello_) return;

    const auto interval = fresh() ? kHelloInterval : context_->in_raid ? kInitialRaidRetry : kUnsupportedIdleRetry;
    next_hello_ = now_ + interval;
    send(Action::Hello);
}

bool Adapter::packet(uint16_t opcode, const void* data, std::size_t size)
{
    if (!selected_ || opcode != kOpcode) return false;
    auto snapshot = ParseSnapshot(data, size);
    if (!snapshot) return true;

    observe(true);
    if (!context_ || context_->in_raid != ((snapshot->flags & kInRaid) != 0)) return true;
    if (snapshot_ && snapshot_->check_id == snapshot->check_id &&
        snapshot_->phase != Phase::Active && snapshot->phase == Phase::Active) return true;

    snapshot_ = std::move(snapshot);
    received_at_ = now_;
    next_hello_ = now_ + kHelloInterval;
    return true;
}

std::string_view Adapter::request(std::string_view operation, const HailstoneArgs* args)
{
    if (!context_ || !context_->in_raid || !fresh()) return "Raid Ready Check is unavailable.";
    if (now_ < next_action_) return "Wait a moment before sending another ready check action.";

    const bool leader = (snapshot_->flags & kLeader) != 0;
    if (operation == "start") {
        if (!leader) return "Only the raid leader can start a ready check.";
        if (snapshot_->phase == Phase::Active) return "A ready check is already active.";
        next_action_ = now_ + kActionInterval;
        return send(Action::Start) ? "" : "The ready check could not be sent.";
    }

    const auto check_id = services::decimal(host_, args, 1);
    if (!check_id || *check_id != snapshot_->check_id || snapshot_->phase != Phase::Active || remaining_ms() == 0) {
        return "That ready check has closed or changed.";
    }
    if (operation == "end") {
        if (!leader) return "Only the raid leader can end a ready check.";
        next_action_ = now_ + kActionInterval;
        return send(Action::End, *check_id) ? "" : "The ready check could not be ended.";
    }

    if (!(snapshot_->flags & kCanRespond)) return "This character cannot respond to the current ready check.";
    const auto answer = services::argument(host_, args, 2, 12);
    if (!answer || (*answer != "ready" && *answer != "not_ready")) return "Choose Ready or Not Ready.";
    const auto choice = *answer == "ready" ? Status::Ready : Status::NotReady;
    next_action_ = now_ + kActionInterval;
    return send(Action::Respond, *check_id, choice) ? "" : "The ready check response could not be sent.";
}

int Adapter::callback(void* self, const HailstoneArgs* args, HailstoneOut* output) noexcept
{
    if (!self) return 0;
    auto& adapter = *static_cast<Adapter*>(self);
    return services::guarded_ui(adapter.host_, [&] { return adapter.ui(args, output); });
}

int Adapter::ui(const HailstoneArgs* args, HailstoneOut* output)
{
    const services::Output out{host_, output};
    const auto operation = services::argument(host_, args, 0, 16);
    if (!operation || !out.valid()) return 0;
    if (*operation == "start" || *operation == "end" || *operation == "respond") {
        observe(true);
        const auto error = request(*operation, args);
        out.begin();
        out.number("ok", error.empty());
        if (!error.empty()) out.string("error", error);
        out.end();
        return 1;
    }
    if (*operation != "state") return 0;

    observe();
    const bool available = fresh();
    const bool in_raid = context_ && context_->in_raid;
    const auto phase = available ? snapshot_->phase : Phase::Idle;
    const auto remaining = available ? remaining_ms() : 0;
    Status own_answer = Status::NoResponse;
    out.begin();
    out.number("available", available);
    out.number("in_raid", in_raid);
    out.number("can_start", available && in_raid && (snapshot_->flags & kLeader));
    out.number("can_respond", available && in_raid && remaining > 0 && (snapshot_->flags & kCanRespond));
    out.string("phase", kPhaseNames[static_cast<uint8_t>(phase)]);
    out.identity("check_id", available ? snapshot_->check_id : 0);
    out.number("remaining_seconds", (static_cast<uint64_t>(remaining) + 999) / 1000);
    out.array("members");
    if (available && in_raid) {
        for (const auto& member : snapshot_->members) {
            if (raid::same_name(member.name, context_->name)) own_answer = member.status;
            out.begin();
            out.string("name", member.name);
            out.number("group", member.group);
            out.string("status", kStatusNames[static_cast<uint8_t>(member.status)]);
            out.string("capability", kCapabilityNames[static_cast<uint8_t>(member.capability)]);
            out.end();
        }
    }
    out.end();
    out.string("self_status", kStatusNames[static_cast<uint8_t>(own_answer)]);
    out.end();
    return 1;
}

}  // namespace bastion::ready_check
