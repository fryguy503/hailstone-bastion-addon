// SPDX-License-Identifier: MIT
#include "raid_ready_check.h"
#include "citadel_test_host.h"

#include <iostream>

struct HailstoneArgs { std::vector<HailstoneValue> values; };
struct HailstoneOut {
    unsigned depth = 0;
    std::string key;
    std::map<std::string, int64_t> numbers;
    std::map<std::string, std::string> strings;
    std::vector<std::map<std::string, std::string>> members;
};

namespace rc = bastion::ready_check;
using citadel_test::Bytes;
using citadel_test::check;
using citadel_test::put;
using citadel_test::text;

namespace {
constexpr uint64_t kCheck = UINT64_C(0xfedcba9876543210);
constexpr auto kCheckString = "18364758544493064720";

Bytes snapshot(rc::Phase phase, uint8_t flags, std::initializer_list<rc::Member> members = {},
               uint32_t remaining = 0)
{
    Bytes bytes(24 + 68 * members.size());
    put(bytes, 0, 0x4B435242);
    put(bytes, 4, 1, 2);
    put(bytes, 6, 5, 1);
    put(bytes, 7, static_cast<uint8_t>(phase), 1);
    put(bytes, 8, phase == rc::Phase::Idle ? 0 : kCheck, 8);
    put(bytes, 16, remaining);
    put(bytes, 20, members.size(), 2);
    put(bytes, 22, flags, 1);
    std::size_t offset = 24;
    for (const auto& member : members) {
        text(bytes, offset, member.name);
        put(bytes, offset + 64, member.group, 1);
        put(bytes, offset + 65, static_cast<uint8_t>(member.status), 1);
        put(bytes, offset + 66, static_cast<uint8_t>(member.capability), 1);
        offset += 68;
    }
    return bytes;
}

void codec()
{
    const auto bytes = snapshot(rc::Phase::Active, rc::kInRaid | rc::kCanRespond, {
        {"Fixture", 1, rc::Status::Ready, rc::Capability::ReadyCheck},
        {"Legacy", 12, rc::Status::Legacy, rc::Capability::Legacy},
        {"Zoning", 0, rc::Status::NoResponse, rc::Capability::Unknown},
    }, 4532);
    auto result = rc::ParseSnapshot(bytes.data(), bytes.size());
    check(result && result->check_id == kCheck && result->remaining_ms == 4532 && result->members.size() == 3,
          "snapshot wire fields changed");
    check(result->members[1].group == 12 && result->members[1].status == rc::Status::Legacy &&
          result->members[2].capability == rc::Capability::Unknown, "status or capability offsets changed");
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        check(!rc::ParseSnapshot(bytes.data(), size), "truncated snapshot accepted");
    }

    auto bad = bytes;
    bad.push_back(std::byte{});
    check(!rc::ParseSnapshot(bad.data(), bad.size()), "trailing snapshot bytes accepted");
    for (const std::size_t offset : {0, 4, 6, 7, 20, 22, 23, 88, 89, 90, 91}) {
        bad = bytes;
        bad[offset] = std::byte{255};
        check(!rc::ParseSnapshot(bad.data(), bad.size()), "out-of-range field accepted");
    }
    bad = bytes;
    bad[24 + 68 + 66] = std::byte{0};
    check(!rc::ParseSnapshot(bad.data(), bad.size()), "unconfirmed capability appeared as legacy");
    const auto request = rc::MakeRequest(rc::Action::Respond, kCheck, rc::Status::NotReady);
    check(request == Bytes({std::byte{0x42}, std::byte{0x52}, std::byte{0x43}, std::byte{0x4b},
                            std::byte{1}, std::byte{0}, std::byte{3}, std::byte{2},
                            std::byte{0x10}, std::byte{0x32}, std::byte{0x54}, std::byte{0x76},
                            std::byte{0x98}, std::byte{0xba}, std::byte{0xdc}, std::byte{0xfe}}),
          "response request no longer matches the version 1 wire layout");
}

struct Rig : citadel_test::Host {
    rc::Adapter addon;
    bool in_raid = true;
    decltype(PluginHostApi::get_player) get_player = api.get_player;

    Rig()
    {
        api.get_player = [](void* host, HailstonePlayer* out) {
            auto& rig = *static_cast<Rig*>(static_cast<citadel_test::Host*>(host));
            const auto result = rig.get_player(host, out);
            out->in_raid = rig.in_raid;
            return result;
        };
        api.arg = [](void*, const HailstoneArgs* args, std::size_t i, HailstoneValue* out) {
            if (i >= args->values.size()) return 0;
            *out = args->values[i];
            return 1;
        };
        api.out_begin_table = [](void*, HailstoneOut* out) {
            if (++out->depth == 3) out->members.emplace_back();
        };
        api.out_begin_array = [](void*, HailstoneOut* out) { ++out->depth; };
        api.out_end = [](void*, HailstoneOut* out) { --out->depth; };
        api.out_key = [](void*, HailstoneOut* out, const char* key) { out->key = key; };
        api.out_value = [](void*, HailstoneOut* out, const HailstoneValue* value) {
            if (out->depth == 3) {
                out->members.back()[out->key] = value->kind == HAILSTONE_STRING ? value->s : std::to_string(value->i);
            } else if (value->kind == HAILSTONE_INT) {
                out->numbers[out->key] = value->i;
            } else if (value->kind == HAILSTONE_STRING) {
                out->strings[out->key] = value->s;
            }
        };
        addon.set_host(&api);
        addon.initialize();
        addon.peer(true);
        addon.game_state(GAMESTATE_INGAME);
    }

    HailstoneOut call(std::initializer_list<const char*> args)
    {
        HailstoneArgs input;
        for (auto value : args) {
            HailstoneValue param{};
            param.kind = HAILSTONE_STRING;
            param.s = value;
            input.values.push_back(param);
        }
        HailstoneOut result;
        const auto fn = functions.at("readycheck");
        check(fn.fn(fn.self, &input, &result) == 1 && result.depth == 0, "ready check Lua bridge failed");
        return result;
    }

    bool packet(const Bytes& data)
    {
        return addon.packet(rc::kOpcode, data.data(), data.size());
    }

    void expect_request(rc::Action action, uint64_t id = 0, rc::Status status = rc::Status::NoResponse) const
    {
        check(sent.back().opcode == 0x7D26 && sent.back().bytes == rc::MakeRequest(action, id, status),
              "incorrect request on the zone stream");
    }
};

void lifecycle()
{
    Rig rig;
    rig.addon.pulse();
    rig.expect_request(rc::Action::Hello);
    check(rig.call({"state"}).numbers.at("available") == 0 && rig.call({"start"}).numbers.at("ok") == 0,
          "server support was inferred from a probe");
    check(!rig.addon.packet(rc::kOpcode + 1, nullptr, 0), "unrelated packet consumed");
    check(rig.packet(snapshot(rc::Phase::Idle, rc::kInRaid | rc::kLeader)), "hello acknowledgment not consumed");
    check(rig.call({"state"}).numbers.at("can_start") == 1 && rig.call({"start"}).numbers.at("ok") == 1,
          "negotiated leader start unavailable");
    rig.expect_request(rc::Action::Start);
    check(rig.call({"state"}).strings.at("phase") == "idle", "start was optimistically acknowledged");

    rig.now += 400;
    rig.packet(snapshot(rc::Phase::Active, rc::kInRaid | rc::kCanRespond, {
        {"fixture", 1, rc::Status::NoResponse, rc::Capability::ReadyCheck},
        {"Remote", 0, rc::Status::NoResponse, rc::Capability::Unknown},
        {"Legacy", 12, rc::Status::Legacy, rc::Capability::Legacy},
    }, 45000));
    auto state = rig.call({"state"});
    check(state.strings.at("check_id") == kCheckString && state.numbers.at("remaining_seconds") == 45 &&
          state.members.at(1).at("capability") == "unknown" && state.members.at(2).at("status") == "legacy",
          "complete server snapshot not presented to Lua");
    const auto sent_before_vote = rig.sent.size();
    check(rig.call({"end", kCheckString}).numbers.at("ok") == 0 &&
          rig.call({"respond", "1234", "ready"}).numbers.at("ok") == 0 &&
          rig.call({"respond", kCheckString, "legacy"}).numbers.at("ok") == 0 && rig.sent.size() == sent_before_vote,
          "invalid vote, stale check or nonleader end reached the wire");
    check(rig.call({"respond", kCheckString, "not_ready"}).numbers.at("ok") == 1, "member vote unavailable");
    rig.expect_request(rc::Action::Respond, kCheck, rc::Status::NotReady);
    check(rig.call({"state"}).strings.at("self_status") == "no_response", "vote should wait for acknowledgment");

    rig.now += 600;
    rig.packet(snapshot(rc::Phase::Active, rc::kInRaid | rc::kLeader | rc::kCanRespond, {
        {"fixture", 1, rc::Status::NotReady, rc::Capability::ReadyCheck},
    }, 1100));
    check(rig.call({"state"}).strings.at("self_status") == "not_ready", "authoritative vote missing");
    check(rig.call({"end", kCheckString}).numbers.at("ok") == 1, "authenticated end unavailable");
    rig.expect_request(rc::Action::End, kCheck);
    rig.now += 1100;
    check(rig.call({"state"}).numbers.at("can_respond") == 0 &&
          rig.call({"respond", kCheckString, "ready"}).numbers.at("ok") == 0, "late vote was permitted");
    rig.packet(snapshot(rc::Phase::Expired, rc::kInRaid | rc::kLeader, {
        {"fixture", 1, rc::Status::NotReady, rc::Capability::ReadyCheck},
    }));
    check(rig.call({"state"}).strings.at("phase") == "expired", "closed results lost");

    rig.addon.begin_zone();
    check(rig.call({"state"}).numbers.at("available") == 0 && rig.call({"state"}).members.empty(),
          "old zone check stayed visible");
    ++rig.zone;
    rig.addon.game_state(GAMESTATE_INGAME);
    rig.addon.pulse();
    rig.expect_request(rc::Action::Hello);
    rig.in_raid = false;
    rig.packet(snapshot(rc::Phase::Idle, 0));
    check(rig.call({"state"}).numbers.at("in_raid") == 0 && rig.call({"start"}).numbers.at("ok") == 0,
          "out-of-raid session retained controls");
    rig.addon.set_host(nullptr);
    check(!rig.functions.contains("readycheck"), "ready check callback leaked at unload");
}
}  // namespace

int main()
{
    try {
        codec();
        lifecycle();
        std::cout << "Raid Ready Check passed " << citadel_test::checks << " checks\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
