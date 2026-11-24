#include "raid_markers_addon.h"
#include "received_tools_test_host.h"

#include <algorithm>
#include <array>
#include <cstring>

using namespace bastion::raid_markers;
using citadel_test::check;

namespace {

constexpr std::uint16_t kSpecialMessage = 0x0083;
constexpr std::uint16_t kRaidUpdate = 0x3973;
constexpr std::size_t kPeerOffset = 24;
constexpr std::size_t kPeerSize = 64;
constexpr std::uint64_t kSendInterval = 3200;
constexpr std::string_view kManagerCommand = "/say #raidmanager list";
constexpr std::string_view kManagerList = "Raid managers: Manager";
constexpr std::string_view kNoManagers = "This raid has no raid managers.";
constexpr std::array<std::string_view, 4> kRoster{"Leader", "Manager", "Assist", "Viewer"};

struct Seat : citadel_test::ReceivedHost {
    Adapter adapter;
    std::string name;
    std::string effective_leader;
    bool main_assist = true;
    bool assist_present = true;
    bool leader_present = true;
    bool raid_active = true;
    bool placement_supported = true;
    unsigned cancellations = 0;
    HailstoneWorldPlacement placement{};
    std::vector<std::uint64_t> submitted;

    static Seat& self(void* host) { return *static_cast<Seat*>(static_cast<citadel_test::Host*>(host)); }

    explicit Seat(std::string character, std::string effective = "Leader")
        : name(std::move(character)), effective_leader(std::move(effective)) {
        placement.size = sizeof(placement);
        placement.zone_id = 202;
        placement.instance_id = 6;
        placement.status = HAILSTONE_WORLD_PLACEMENT_INACTIVE;
        api.caps = [](void*) -> std::uint32_t {
            return HAILSTONE_CAP_READ | HAILSTONE_CAP_UI | HAILSTONE_CAP_EXTEND | HAILSTONE_CAP_PACKETS | HAILSTONE_CAP_CHAT;
        };
        api.world_capabilities = [](void* host) -> std::uint32_t {
            return self(host).placement_supported ? HAILSTONE_WORLD_PLACEMENT : HAILSTONE_WORLD_GEOMETRY;
        };
        api.get_player = [](void* host, HailstonePlayer* out) {
            const auto& seat = self(host);
            *out = {};
            out->size = sizeof(*out);
            out->in_zone = seat.in_zone;
            out->in_raid = 1;
            std::memcpy(out->name, seat.name.c_str(), seat.name.size() + 1);
            return 1;
        };
        api.get_raid_data_bulk = [](void* host, unsigned char* out, std::int32_t capacity) -> std::int32_t {
            const auto& seat = self(host);
            HS_BulkRaidHeader header{};
            header.size = sizeof(header);
            header.version = 1;
            header.recordSize = HS_BULK_RAID_RECORD_SIZE;
            if (!seat.raid_active) {
                if (capacity < static_cast<std::int32_t>(sizeof(header))) return 0;
                std::memcpy(out, &header, sizeof(header));
                return static_cast<std::int32_t>(sizeof(header));
            }
            header.active = 1;
            header.count = header.total = static_cast<std::uint32_t>(kRoster.size());
            std::array<HS_BulkRaidData, kRoster.size()> records{};
            for (std::size_t i = 0; i < records.size(); ++i) {
                auto& record = records[i];
                const auto name = kRoster[i];
                std::memcpy(record.name, name.data(), name.size());
                if (name == seat.effective_leader) {
                    header.leaderIndex = static_cast<std::uint32_t>(i);
                    record.flags |= HS_BULK_RAID_LEADER;
                }
                if (name == "Assist" && seat.main_assist)
                    record.flags |= HS_BULK_RAID_MAIN_ASSIST;
                if ((name != "Assist" || seat.assist_present) && (name != "Leader" || seat.leader_present))
                    record.flags |= HS_BULK_RAID_PRESENT;
            }
            const auto bytes = static_cast<std::int32_t>(sizeof(header) + sizeof(records));
            if (capacity < bytes) return 0;
            std::memcpy(out, &header, sizeof(header));
            std::memcpy(out + sizeof(header), records.data(), sizeof(records));
            return bytes;
        };
        api.world_placement_start = [](void* host) {
            self(host).placement.status = HAILSTONE_WORLD_PLACEMENT_AIMING_INVALID;
            return 1;
        };
        api.world_placement_get = [](void* host, HailstoneWorldPlacement* out) {
            *out = self(host).placement;
            if (out->status == HAILSTONE_WORLD_PLACEMENT_CONFIRMED)
                self(host).placement.status = HAILSTONE_WORLD_PLACEMENT_INACTIVE;
            return 1;
        };
        api.world_placement_cancel = [](void* host) {
            ++self(host).cancellations;
            self(host).placement.status = HAILSTONE_WORLD_PLACEMENT_CANCELLED;
        };
        api.world_submit = [](void* host, const HailstoneWorldSubmission* submission) {
            self(host).submitted.push_back(submission->tag);
            return 1;
        };
        adapter.set_host(&api);
        adapter.initialize();
        adapter.peer(true);
        adapter.game_state(GAMESTATE_INGAME);
    }

    bool action(const char* operation, std::uint8_t marker = 0) {
        HailstoneValue command{}, index{};
        command.kind = HAILSTONE_STRING;
        command.s = operation;
        index.kind = HAILSTONE_INT;
        index.i = marker;
        HailstoneArgs args{{command, index}};
        HailstoneOut out;
        const auto function = functions.at("raid_markers");
        check(function.fn(function.self, &args, &out) == 1 && out.depth == 0, "Marker UI call failed");
        return out.numbers.at("ok") != 0;
    }

    bool can_place() { return call("raid_markers", "state").numbers.at("can_place") != 0; }

    void tick(std::uint64_t instant) {
        now = instant;
        adapter.pulse();
    }

    void place(std::uint8_t id, float x) {
        check(action("aim", id), "Eligible raid caller could not start placement");
        placement.position[0] = x;
        placement.position[1] = 12;
        placement.position[2] = 20;
        placement.status = HAILSTONE_WORLD_PLACEMENT_CONFIRMED;
        adapter.pulse();
    }

    std::size_t draw() {
        submitted.clear();
        HailstoneWorldFrame frame{};
        frame.size = sizeof(frame);
        frame.frame = frame.epoch = 1;
        frame.capabilities = HAILSTONE_WORLD_GEOMETRY;
        frame.maximum_vertices = 1024;
        frame.maximum_batches = 8;
        adapter.world_frame(&frame);
        return submitted.size();
    }

    bool packet(std::uint16_t opcode, const Bytes& bytes) {
        return adapter.packet(opcode, bytes.data(), bytes.size());
    }

    bool reply(std::string_view text, std::uint8_t chat_type = 10) {
        Bytes reply(25 + text.size());
        citadel_test::put(reply, 3, chat_type);
        citadel_test::text(reply, 24, text);
        return packet(kSpecialMessage, reply);
    }
};

Bytes relay(Bytes bytes, std::string_view authenticated_sender) {
    std::fill_n(bytes.begin() + kPeerOffset, kPeerSize, std::byte{});
    citadel_test::text(bytes, kPeerOffset, authenticated_sender);
    return bytes;
}

Bytes sent_by(const Seat& seat, std::string_view intended_recipient = {}) {
    check(!seat.sent.empty() && seat.sent.back().opcode == kOpcode, "Sentinel relay packet was not sent");
    const auto& bytes = seat.sent.back().bytes;
    check(bytes.size() > kPeerOffset + kPeerSize, "Relay packet is undersized");
    const std::span name(bytes.data() + kPeerOffset, kPeerSize);
    const auto end = std::find(name.begin(), name.end(), std::byte{});
    check(end != name.end() && std::string_view(reinterpret_cast<const char*>(name.data()),
          static_cast<std::size_t>(end - name.begin())) == intended_recipient, "Incorrect relay destination");
    return relay(bytes, seat.name);
}

Message read(const Bytes& bytes, Kind expected) {
    Received incoming;
    check(decode(bytes, incoming) == DecodeResult::Valid && incoming.message.kind == expected,
          "Unexpected marker kind on the relay");
    return incoming.message;
}

Bytes raid_update(std::uint32_t action, std::string_view name) {
    Bytes bytes(140);
    citadel_test::put(bytes, 0, action);
    citadel_test::text(bytes, 4, name);
    citadel_test::text(bytes, 72, name);
    return bytes;
}

void raid_flow() {
    Seat leader("Leader"), manager("Manager", "Manager"), assist("Assist"), viewer("Viewer");
    check(!leader.reply(kManagerList), "An unsolicited manager reply selected a coordinator");
    leader.tick(1000);
    check(leader.commands.size() == 1 && leader.commands.back() == kManagerCommand && leader.sent.empty() &&
          !leader.can_place(), "The roster's leader flag alone granted authority");
    check(!leader.reply(kManagerList, 11) && !leader.reply("Raid managers: Manager, malformed entry") &&
          !leader.reply("prefix Raid managers: Manager"), "Nonmatching server text passed the coordinator proof");
    check(leader.reply(kManagerList), "Valid leader-only server reply was not consumed");
    leader.tick(1000);
    const auto initial_wire = sent_by(leader);
    const auto initial = read(initial_wire, Kind::Snapshot);
    check(leader.can_place() && initial.nonce != 0 && initial.markers.empty(), "Coordinator did not open an empty session");

    manager.tick(1000);
    check(!manager.can_place() && manager.sent.empty() && manager.commands.size() == 1,
          "Manager's effective-leader flag elected it as coordinator");
    check(manager.reply("Only the raid leader can add or remove raid managers."), "Manager denial was ignored");
    for (auto* client : {&manager, &assist, &viewer})
        check(client->packet(kOpcode, initial_wire), "Initial snapshot was not recognized");
    check(manager.can_place() && assist.can_place() && !viewer.can_place() && !viewer.action("aim", 1),
          "Marker controls did not follow current raid roles");

    leader.place(1, 10);
    check(leader.sent.size() == 1 && leader.draw() == 1, "The local marker ignored rendering or the relay throttle");
    leader.tick(1000 + kSendInterval - 1);
    check(leader.sent.size() == 1, "Broadcast bypassed the Sentinel transfer floor");
    leader.tick(1000 + kSendInterval);
    const auto first_wire = sent_by(leader);
    check(read(first_wire, Kind::Snapshot).markers == std::vector<Marker>{{1, 202, 6, 10, 12, 20}},
          "Leader placement did not synchronize");
    check(viewer.packet(kOpcode, first_wire), "Viewer did not consume the placed snapshot");
    viewer.tick(1000 + kSendInterval);
    check(viewer.draw() == 1, "Ordinary raid viewer did not render the shared marker");

    assist.tick(1000 + kSendInterval);
    leader.adapter.begin_zone();
    leader.adapter.world_login(true);
    leader.adapter.peer(false);
    leader.adapter.game_state(GAMESTATE_PRECHARSELECT);
    leader.placement.instance_id = 7;
    leader.adapter.peer(true);
    leader.adapter.game_state(GAMESTATE_INGAME);
    leader.tick(1000 + kSendInterval);
    assist.placement.instance_id = 7;
    assist.place(1, 40);
    const auto direct_assist = sent_by(assist, "Leader");
    check(read(direct_assist, Kind::Set).nonce == initial.nonce, "Assist did not use the coordinator nonce");
    check(leader.packet(kOpcode, direct_assist) && leader.commands.size() == 1,
          "Delegated assist edit was not immediately authorized from the roster");
    leader.tick(1000 + 2 * kSendInterval);
    const auto replacement_wire = sent_by(leader);
    const auto replacement = read(replacement_wire, Kind::Snapshot);
    check(replacement.markers == std::vector<Marker>{{1, 202, 7, 40, 12, 20}},
          "A same-type placement did not replace the original raid slot");
    check(viewer.packet(kOpcode, replacement_wire) && viewer.packet(kOpcode, first_wire),
          "Viewer did not identify a current or stale marker snapshot");
    viewer.tick(1000 + 2 * kSendInterval);
    check(viewer.draw() == 0, "A marker leaked into a different instance or stale snapshot won");

    Seat late("Viewer");
    late.placement.instance_id = 7;
    late.tick(1000 + 2 * kSendInterval);
    check(late.packet(kOpcode, relay(replacement_wire, "Assist")) && late.draw() == 0,
          "A raid member other than the real leader supplied a snapshot");
    check(late.packet(kOpcode, replacement_wire) && late.draw() == 1,
          "A late joiner did not accept the current snapshot for the exact instance");

    manager.placement.instance_id = 7;
    manager.tick(1000 + 2 * kSendInterval);
    manager.place(2, 50);
    const auto direct_manager = sent_by(manager, "Leader");
    check(read(direct_manager, Kind::Set).markers.front().id == 2, "Manager made an incorrect placement request");
    check(leader.packet(kOpcode, relay(direct_manager, "Outsider")) && leader.commands.size() == 1,
          "A non-member started a manager authorization query");
    check(leader.packet(kOpcode, direct_manager) && leader.commands.size() == 2 && leader.draw() == 1,
          "Manager edit bypassed fresh authorization or did not query the server");
    check(leader.reply(kManagerList) && leader.draw() == 2, "Fresh manager reply did not authorize the edit");
    leader.tick(1000 + 3 * kSendInterval);
    const auto manager_snapshot = read(sent_by(leader), Kind::Snapshot);
    check(manager_snapshot.markers.size() == 2, "Manager edit was not synchronized");

    manager.tick(1000 + 3 * kSendInterval);
    check(manager.action("clear", 2), "Manager cannot request clearing a marker");
    manager.adapter.pulse();
    check(leader.packet(kOpcode, sent_by(manager, "Leader")) && leader.commands.size() == 3,
          "Cached manager status bypassed a fresh check on mutation");
    check(leader.reply(kNoManagers) && leader.draw() == 2, "A revoked manager cleared the marker");

    leader.assist_present = false;
    assist.leader_present = false;
    leader.tick(1000 + 4 * kSendInterval);
    assist.tick(1000 + 4 * kSendInterval);
    check(!assist.can_place() && !assist.action("aim", 1), "A remote assist could arm placement");
    const auto remote = encode(Message{Kind::Clear, initial.nonce, 0, 1, {}}, 819, "Leader");
    check(remote && leader.packet(kOpcode, relay(*remote, "Assist")) && leader.reply(kNoManagers) &&
          leader.draw() == 2, "A stale remote assist role authorized a marker edit");

    assist.leader_present = true;
    assist.tick(1000 + 4 * kSendInterval);
    check(assist.action("aim", 3), "Assist could not arm a second marker");
    const auto packets = assist.sent.size();
    assist.adapter.begin_zone();
    check(assist.cancellations == 1 && assist.draw() == 0 && !assist.can_place(), "Zoning left aim or markers active");
    assist.placement.status = HAILSTONE_WORLD_PLACEMENT_CONFIRMED;
    assist.tick(1000 + 4 * kSendInterval);
    check(assist.sent.size() == packets, "A stale confirmation sent a packet during zoning");
}

void roster_rebuild_preserves_markers() {
    Seat leader("Leader");
    leader.tick(1000);
    check(leader.reply(kNoManagers), "Leader authorization failed");
    leader.tick(1000);
    leader.place(1, 10);
    check(leader.draw() == 1, "Roster rebuild fixture could not place a marker");
    check(!leader.packet(kRaidUpdate, raid_update(5, "Leader")), "Targeted raid disband was consumed");
    leader.raid_active = false;
    leader.tick(1050);
    check(leader.draw() == 0 && !leader.can_place(), "Temporary roster loss left markers active");
    check(!leader.packet(kRaidUpdate, raid_update(8, "Leader")), "Raid create was consumed");
    leader.raid_active = true;
    leader.tick(1100);
    check(leader.draw() == 1 && leader.can_place(), "The same raid lost its slots during the roster rebuild");
    check(!leader.packet(kRaidUpdate, raid_update(5, "RaidMember")), "Raid-wide disband was consumed");
    check(leader.draw() == 0 && !leader.can_place(), "Disbanding a raid retained its markers");
}

void placement_needs_capability() {
    Seat leader("Leader");
    leader.placement_supported = false;
    leader.tick(1000);
    check(!leader.can_place() && !leader.action("aim", 1) && leader.commands.empty(),
          "A host without the placement capability started a marker session");
    leader.placement_supported = true;
    leader.tick(2000);
    check(leader.commands.size() == 1 && leader.reply(kNoManagers) && leader.can_place(),
          "A compatible host could not start a marker session");
}

} // namespace

int main() {
    try {
        raid_flow();
        roster_rebuild_preserves_markers();
        placement_needs_capability();
        std::cout << citadel_test::checks << " raid marker integration checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
