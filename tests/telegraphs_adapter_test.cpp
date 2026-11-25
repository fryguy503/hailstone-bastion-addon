#include "hailstone_plugin.h"
#include "telegraphs_adapter.h"
#include "telegraphs_protocol.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace tel = bastion::telegraphs;
namespace {
std::size_t checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
bool near(float a, float b) { return std::abs(a - b) < .001f; }
void put(tel::Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = std::byte(static_cast<std::uint8_t>(value >> (8 * i)));
}
void floating(tel::Bytes& bytes, std::size_t at, float value) { put(bytes, at, std::bit_cast<std::uint32_t>(value)); }
tel::Bytes control(tel::Action action, std::uint32_t id = 0, std::uint32_t revision = 0) {
    tel::Bytes b{}; put(b, 0, 0x54454c31); b[4] = std::byte{3}; b[5] = std::byte(static_cast<std::uint8_t>(action));
    put(b, 16, 7); put(b, 20, id); put(b, 24, revision); put(b, 40, 1000);
    return b;
}
tel::Bytes circle(std::uint32_t id = 42, float x = 12, float y = 34, float z = 0) {
    auto b = control(tel::Action::Create, id, 3);
    b[6] = std::byte{1}; b[8] = std::byte{0x84}; b[9] = std::byte{1}; // Filled|Clamp|Required
    b[12] = std::byte{1}; put(b, 44, 1000); put(b, 48, 5000); put(b, 52, 6000);
    floating(b, 56, x); floating(b, 60, y); floating(b, 64, z); floating(b, 72, 8); floating(b, 84, 8);
    put(b, 92, 0xc8123456);
    return b;
}
struct Saved {
    std::uint64_t frame, tag, revision;
    std::uint32_t epoch;
    bool admitted;
    std::vector<HailstoneWorldVertex> vertices;
};
struct Probe { float x, y, z; };
struct FallbackOutput {
    unsigned tables = 0;
    std::string key;
    std::map<std::string, std::string> text;
    std::map<std::string, std::int64_t> numbers;
};
// Direct adapter fixture. Production plugin packet routing is covered by the
// standalone plugin integration tests, not by this renderer host model.
struct AdapterHarness {
    tel::Adapter value;
    bool selected = false;
    void set_host(const PluginHostApi *host) { value.set_host(host); }
    void initialize() { value.initialize(); }
    void shutdown() { value.set_host(nullptr); }
    void pulse() { value.pulse(); }
    void begin_zone() { value.begin_zone(); }
    void game_state(int state) { value.game_state(state); }
    AdapterHarness *hooks() { return this; }
    void world_frame(const HailstoneWorldFrame *frame) { value.world_frame(frame); }
    void world_frame_result(const HailstoneWorldReceipt *receipt) { value.world_result(receipt); }
    int packet(int stream, int direction, HailstonePacket *packet) {
        if (!packet) return 0;
        if (stream == HAILSTONE_STREAM_WORLD && direction == HAILSTONE_IN && packet->opcode == 0x7ceb && packet->size >= 42) {
            selected = packet->data && std::memcmp(packet->data + 36, "BasDev", 6) == 0;
            value.peer(selected); return 0;
        }
        if (selected && stream == HAILSTONE_STREAM_ZONE && direction == HAILSTONE_IN && packet->opcode == tel::kOpcode) {
            value.receive(packet->data, packet->size); return 1;
        }
        return 0;
    }
};
struct Rig {
    AdapterHarness adapter;
    AdapterHarness *addon = &adapter;
    PluginHostApi api{};
    std::uint64_t now = 100, serial = 0;
    std::uint32_t epoch = 7, caps = 31, maximum_vertices = 98304, maximum_batches = 256, remaining_queries = 262144;
    std::uint32_t collision_revision = 1;
    bool regional_cache_valid = true;
    std::uint32_t regional_revision = 0;
    bool admission = true, wire_success = true;
    std::vector<tel::Packet> sent;
    std::vector<Saved> submissions;
    std::vector<Probe> probes;
    std::size_t anchor_queries = 0;
    HailstoneUiFn fallback_ui = nullptr;
    void* fallback_ui_context = nullptr;
    std::string config_path;
    std::map<std::uint32_t, HailstoneWorldAnchor> actors;
    std::function<int(float, float, float, float&)> sample = [](float, float, float probe, float& z) {
        z = 0; return probe >= 0 ? HAILSTONE_WORLD_RESOLVED : HAILSTONE_WORLD_MISSING;
    };
    std::function<int(const HailstoneWorldSurfacePatch&,HailstoneWorldSurfacePatchResult&)> patch_sample=[](const auto&,auto&){return HAILSTONE_WORLD_PATCH_PLANAR;};
    Rig() {
        // Each Rig models a later host attachment in one process; the protocol
        // intentionally remembers recent attempted Hello ticks across resets.
        static std::uint64_t next_clock = 100;
        now = next_clock; next_clock += 10000;
        api.abi = HAILSTONE_PLUGIN_ABI; api.size = sizeof(api); api.host = this;
        api.send_packet = [](void* context, int stream, std::uint16_t opcode, const void* data, std::size_t count) {
            auto& r = *static_cast<Rig*>(context);
            check(stream == HAILSTONE_STREAM_ZONE && opcode == tel::kOpcode, "Adapter used a non-TEL gameplay wire path");
            const auto p = tel::decode({static_cast<const std::byte*>(data), count});
            check(p.has_value(), "Adapter emitted invalid TEL1 bytes");
            if (r.wire_success) r.sent.push_back(*p);
            return r.wire_success ? 1 : 0;
        };
        api.world_capabilities = [](void* context) { return static_cast<Rig*>(context)->caps; };
        api.monotonic_ms = [](void* context) { return static_cast<Rig*>(context)->now; };
        api.world_floor_below = [](void* context, std::uint64_t frame, std::uint32_t zone_epoch, float x, float y, float probe, float* z) {
            auto& r = *static_cast<Rig*>(context);
            check(frame == r.serial && zone_epoch == r.epoch && z, "Adapter queried outside the exact current SDK frame");
            r.probes.push_back({x, y, probe});
            return r.sample(x, y, probe, *z);
        };
        api.world_surface_patch=[](void* context,const HailstoneWorldSurfacePatch* query,HailstoneWorldSurfacePatchResult* result) {
            auto& r=*static_cast<Rig*>(context);
            check(query&&result&&query->size==sizeof(*query)&&result->size==sizeof(*result)&&query->frame==r.serial&&query->epoch==r.epoch,
                  "surface patch lost current SDK frame or struct identity");
            check(near(query->maximum_rise,6)&&query->tolerance>0,"surface patch lost bounded local ceiling/tolerance");
            const auto answer=r.patch_sample(*query,*result);
            if(answer==HAILSTONE_WORLD_PATCH_PLANAR) {
                // These adapter scenes are explicitly flat/affine. A square
                // preflight with the wrong candidate plane must be rejected;
                // the independently tested native service proves interiors.
                for(std::size_t n=0;n<3;++n) {
                    float z=0;
                    if(r.sample(query->vertices[n*3],query->vertices[n*3+1],query->vertices[n*3+2]+query->maximum_rise,z)!=HAILSTONE_WORLD_RESOLVED||
                       std::abs(z-query->vertices[n*3+2])>query->tolerance) return static_cast<int>(HAILSTONE_WORLD_PATCH_DEFERRED);
                }
            }
            return answer;
        };
        api.world_anchor = [](void* context, std::uint64_t frame, std::uint32_t zone_epoch, std::uint32_t id, HailstoneWorldAnchor* out) -> int {
            auto& r = *static_cast<Rig*>(context);
            check(frame == r.serial && zone_epoch == r.epoch && out && out->size == sizeof(*out), "Adapter anchor query lost frame/size identity");
            ++r.anchor_queries;
            const auto found = r.actors.find(id);
            if (found == r.actors.end()) return HAILSTONE_WORLD_MISSING;
            *out = found->second; return HAILSTONE_WORLD_RESOLVED;
        };
        api.world_submit = [](void* context, const HailstoneWorldSubmission* value) {
            auto& r = *static_cast<Rig*>(context);
            check(value && value->size == sizeof(*value) && value->frame == r.serial && value->epoch == r.epoch && value->tag &&
                  value->primitive == HAILSTONE_WORLD_TRIANGLES && value->vertex_count % 3 == 0, "Adapter submitted invalid generic geometry identity");
            r.submissions.push_back({value->frame, value->tag, value->revision, value->epoch, r.admission,
                                     {value->vertices, value->vertices + value->vertex_count}});
            return r.admission ? 1 : 0;
        };
        addon->set_host(&api); addon->initialize();
    }
    ~Rig() { addon->shutdown(); }
    Rig(const Rig&) = delete;
    void announce(std::string_view name = "BasDev") {
        std::array<std::uint8_t, 560> bytes{};
        std::memcpy(bytes.data() + 36, name.data(), std::min(name.size(), std::size_t(32)));
        HailstonePacket packet{0x7ceb, bytes.data(), bytes.size()};
        check(addon->packet(HAILSTONE_STREAM_WORLD, HAILSTONE_IN, &packet) == 0, "Adapter dropped server announcement");
    }
    int incoming(const tel::Bytes& bytes, int stream = HAILSTONE_STREAM_ZONE, int direction = HAILSTONE_IN) {
        HailstonePacket packet{tel::kOpcode, reinterpret_cast<std::uint8_t*>(const_cast<std::byte*>(bytes.data())), bytes.size()};
        return addon->packet(stream, direction, &packet);
    }
    HailstoneWorldFrame context() {
        HailstoneWorldFrame f{}; f.size = sizeof(f); f.capabilities = caps; f.frame = ++serial; f.epoch = epoch;
        f.collision_revision = collision_revision;
        f.now_ms = now; f.maximum_batches = maximum_batches; f.maximum_vertices = maximum_vertices; f.remaining_queries = remaining_queries;
        f.viewport[2] = 1920; f.viewport[3] = 1080;
        for (unsigned i = 0; i < 4; ++i) f.view_projection[i * 5] = 1;
        return f;
    }
    void frame() { const auto f = context(); addon->hooks()->world_frame(&f); }
    std::size_t count(tel::Action action) const { return static_cast<std::size_t>(std::count_if(sent.begin(), sent.end(), [action](const auto& p) { return p.action == action; })); }
    void connect() {
        announce(); addon->game_state(GAMESTATE_INGAME); frame();
        check(count(tel::Action::Hello) == 1 && submissions.empty(), "Fresh live scene did not send only a Hello");
        ack();
    }
    void ack() {
        const auto found = std::find_if(sent.rbegin(), sent.rend(), [](const auto& p) { return p.action == tel::Action::Hello; });
        check(found != sent.rend(), "No Hello to acknowledge");
        auto bytes = control(tel::Action::HelloAck); put(bytes, 44, found->server_now_ms);
        check(incoming(bytes) == 1, "Selected private HelloAck reached stock dispatch");
    }
    void result(const Saved& saved, unsigned flags = HAILSTONE_WORLD_RECORDED, int count_delta = 0, std::uint64_t revision_delta = 0) {
        HailstoneWorldReceipt receipt{}; receipt.size = sizeof(receipt); receipt.status = HAILSTONE_WORLD_BATCH_FINISHED;
        receipt.frame = saved.frame; receipt.epoch = saved.epoch; receipt.tag = saved.tag; receipt.revision = saved.revision + revision_delta;
        receipt.vertex_count = static_cast<std::uint32_t>(static_cast<std::int64_t>(saved.vertices.size()) + count_delta); receipt.flags = flags;
        addon->hooks()->world_frame_result(&receipt);
    }
    void finish(std::uint64_t frame, unsigned flags = HAILSTONE_WORLD_SUBMITTED | HAILSTONE_WORLD_PRESENTED, std::uint32_t zone = 0) {
        HailstoneWorldReceipt receipt{}; receipt.size = sizeof(receipt); receipt.status = HAILSTONE_WORLD_FRAME_FINISHED;
        receipt.frame = frame; receipt.epoch = zone ? zone : epoch; receipt.flags = flags;
        addon->hooks()->world_frame_result(&receipt);
    }
    void present(const Saved& saved) { result(saved); finish(saved.frame); addon->pulse(); }
};

void actual_adapter_flow_and_axes() {
    Rig r;
    r.addon->pulse(); r.frame();
    check(r.sent.empty(), "Unselected peer advertised readiness");
    r.connect();
    r.sample = [](float x, float y, float probe, float& z) { z = x + 2 * y; return probe >= z ? HAILSTONE_WORLD_RESOLVED : HAILSTONE_WORLD_MISSING; };
    check(r.incoming(circle(42, 12, 34, 58)) == 1, "Selected marker reached stock dispatch");
    r.now += 16; r.frame();
    check(r.submissions.size() == 1 && r.submissions[0].admitted, "HelloAck/marker did not produce admitted geometry");
    const auto saved = r.submissions[0];
    check(saved.vertices.size() == 24, "Single simple marker was not one complete triangle batch");
    check(near(saved.vertices[0].position[0], 34) && near(saved.vertices[0].position[1], 12) && near(saved.vertices[0].position[2], 58.035f),
          "Raw center12,34 did not cross SDK boundary as zone-file34,12");
    check(near(saved.vertices[1].position[0], 42) && near(saved.vertices[1].position[1], 12), "Heading0 did not point along SDK+X");
    check(std::all_of(saved.vertices.begin(), saved.vertices.end(), [](const auto& v) { return near(v.position[2], v.position[0] + 2 * v.position[1] + .035f); }),
          "Asymmetric terrain queries and output vertices used different axis conventions");
    check(near(saved.vertices[0].color[0], 0x12 / 255.0f) && near(saved.vertices[0].color[3], 200 / 255.0f), "Packed ARGB was not converted to straight SDK color");
    r.result(saved); r.addon->pulse();
    check(r.count(tel::Action::Presented) == 0, "Per-batch receipt alone emitted Presented");
    r.finish(saved.frame); r.addon->pulse();
    check(r.count(tel::Action::Presented) == 1 && r.sent.back().id == 42 && r.sent.back().revision == 3 && r.sent.back().generation == 7,
          "Actual final presentation outcome did not produce exact marker acknowledgement");
    r.result(saved); r.finish(saved.frame); r.addon->pulse();
    check(r.count(tel::Action::Presented) == 1, "Duplicate completion emitted duplicate Presented");
}

void absent_services_and_routing() {
    for (unsigned missing = 0; missing < 9; ++missing) {
        Rig r; r.announce(); r.addon->game_state(GAMESTATE_INGAME);
        switch (missing) {
        case 0: r.api.world_capabilities = nullptr; break;
        case 1: r.api.world_floor_below = nullptr; break;
        case 2: r.api.world_anchor = nullptr; break;
        case 3: r.api.world_submit = nullptr; break;
        case 4: r.api.monotonic_ms = nullptr; break;
        case 5: r.api.send_packet = nullptr; break;
        case 6: r.api.world_surface_patch = nullptr; break;
        case 7: r.caps &= ~static_cast<std::uint32_t>(HAILSTONE_WORLD_SURFACE_PATCH); break;
        default: r.api.size = static_cast<std::uint32_t>(offsetof(PluginHostApi, monotonic_ms)); break;
        }
        r.frame(); r.addon->pulse();
        check(r.sent.empty() && r.submissions.empty(), "Missing/truncated SDK service still advertised TEL readiness");
        check(r.incoming(circle()) == 1, "Selected private marker leaked while services absent");
    }
    Rig r;
    check(r.incoming(circle()) == 0, "Unselected server's opcode was intercepted");
    r.announce();
    check(r.incoming(circle(), HAILSTONE_STREAM_WORLD) == 0 && r.incoming(circle(), HAILSTONE_STREAM_ZONE, HAILSTONE_OUT) == 0,
          "TEL interception ignored stream/direction");
    std::array<std::uint8_t, 2> malformed{};
    HailstonePacket packet{tel::kOpcode, malformed.data(), malformed.size()};
    check(r.addon->packet(HAILSTONE_STREAM_ZONE, HAILSTONE_IN, &packet) == 1, "Malformed selected TEL packet reached stock dispatch");
    packet.data = nullptr; packet.size = 96;
    check(r.addon->packet(HAILSTONE_STREAM_ZONE, HAILSTONE_IN, &packet) == 1, "Null selected TEL packet reached stock dispatch");
}
void malformed_patch_results() {
    for(unsigned malformed=0;malformed<8;++malformed) {
        Rig r;r.connect();
        r.patch_sample=[malformed](const HailstoneWorldSurfacePatch& query,HailstoneWorldSurfacePatchResult& result) {
            for(std::size_t axis=0;axis<3;++axis) result.witness[axis]=(query.vertices[axis]+query.vertices[axis+3]+query.vertices[axis+6])/3;
            std::copy(std::begin(query.vertices),std::end(query.vertices),result.source_face);
            result.flags=HAILSTONE_WORLD_PATCH_HEIGHT_KNOWN|HAILSTONE_WORLD_PATCH_SOURCE_FACE|HAILSTONE_WORLD_PATCH_REGION;
            result.region_count=3;
            for(std::size_t n=0;n<3;++n) {result.region_xy[n*2]=query.vertices[n*3];result.region_xy[n*2+1]=query.vertices[n*3+1];}
            switch(malformed) {
            case 0:return HAILSTONE_WORLD_PATCH_DEFERRED;
            case 1:result.flags=0;break;
            case 2:result.flags=HAILSTONE_WORLD_PATCH_REGION;break;
            case 3:result.flags=HAILSTONE_WORLD_PATCH_HEIGHT_KNOWN|HAILSTONE_WORLD_PATCH_REGION;break;
            case 4:result.region_count=HAILSTONE_WORLD_PATCH_MAX_REGION_VERTICES+1;break;
            case 5:result.region_count=2;break;
            case 6:result.region_xy[1]=std::numeric_limits<double>::quiet_NaN();break;
            default:result.source_face[2]=std::numeric_limits<float>::quiet_NaN();break;
            }
            return HAILSTONE_WORLD_PATCH_MISMATCH;
        };
        r.incoming(circle());r.now+=16;r.frame();r.addon->pulse();
        check(r.submissions.empty()&&r.count(tel::Action::Presented)==0,"unavailable or malformed surface proof submitted/acknowledged a terrain marker");
    }
}

void near_collinear_host_region() {
    for (const double inset : {1.0e-14, .05}) {
        Rig r; r.connect();
        unsigned calls = 0;
        r.patch_sample = [&](const HailstoneWorldSurfacePatch& query, HailstoneWorldSurfacePatchResult& result) {
            ++calls;
            if (calls == 1) return HAILSTONE_WORLD_PATCH_DEFERRED; // Skip the whole-marker planar shortcut.
            if (calls != 2) return HAILSTONE_WORLD_PATCH_PLANAR;
            result.flags = HAILSTONE_WORLD_PATCH_HEIGHT_KNOWN | HAILSTONE_WORLD_PATCH_SOURCE_FACE | HAILSTONE_WORLD_PATCH_REGION;
            result.region_count = 4;
            const float face[] = {-100,-100,0, -100,300,0, 300,-100,0};
            std::copy(std::begin(face), std::end(face), result.source_face);
            for (std::size_t axis = 0; axis < 2; ++axis) {
                const double a = query.vertices[axis], b = query.vertices[axis + 3], c = query.vertices[axis + 6];
                const double middle = (a + b) / 2;
                result.region_xy[axis] = a;
                result.region_xy[axis + 2] = middle + inset * (c - middle);
                result.region_xy[axis + 4] = b;
                result.region_xy[axis + 6] = c;
                result.witness[axis] = static_cast<float>((a + b + c) / 3);
            }
            return HAILSTONE_WORLD_PATCH_MISMATCH;
        };
        r.incoming(circle()); r.now += 16; r.frame();
        check(calls >= 2 && r.submissions.size() == (inset < 1e-12 ? 1u : 0u),
              "Adapter rejected host winding roundoff or admitted a genuinely concave terrain hint");
    }
}

void receipt_failure_paths() {
    for (unsigned failure = 0; failure < 8; ++failure) {
        Rig r; r.connect(); r.incoming(circle()); r.now += 16;
        if (failure == 0) r.admission = false;
        r.frame(); check(r.submissions.size() == 1, "Receipt test produced no geometry attempt");
        const auto saved = r.submissions[0];
        if (failure == 1) r.result(saved, HAILSTONE_WORLD_RECORDED, -3);
        else if (failure == 2) r.result(saved, 0);
        else if (failure == 3) r.result(saved, HAILSTONE_WORLD_RECORDED, 0, 1);
        else if (failure != 4) r.result(saved);
        if (failure == 5) r.finish(saved.frame, HAILSTONE_WORLD_SUBMITTED);
        else if (failure == 6) r.finish(saved.frame, HAILSTONE_WORLD_PRESENTED);
        else if (failure == 7) r.finish(saved.frame, HAILSTONE_WORLD_SUBMITTED | HAILSTONE_WORLD_PRESENTED, r.epoch + 1);
        else r.finish(saved.frame);
        r.addon->pulse();
        check(r.count(tel::Action::Presented) == 0, "Rejected/partial/stale/nonpresented geometry certified a marker");
    }
    Rig r; r.connect(); r.incoming(circle()); r.now += 16; r.frame();
    auto saved = r.submissions[0];
    r.result(saved); r.incoming(control(tel::Action::Cancel, 42, 4)); r.finish(saved.frame); r.addon->pulse();
    check(r.count(tel::Action::Presented) == 0, "Cancelled marker accepted an already issued completion");
    auto replacement = circle(42); put(replacement, 24, 5); r.incoming(replacement); r.now += 16; r.frame();
    saved = r.submissions.back(); r.result(saved); r.finish(saved.frame); r.incoming(control(tel::Action::Cancel, 42, 6)); r.addon->pulse();
    check(r.count(tel::Action::Presented) == 0, "Cancellation before wire send did not retire queued proof");
}

void lifecycle_and_runtime_capability() {
    Rig r; r.connect(); r.incoming(circle()); r.now += 16; r.frame();
    const auto old = r.submissions.back();
    HailstoneWorldReceipt lost{}; lost.size = sizeof(lost); lost.status = HAILSTONE_WORLD_FRAME_UNAVAILABLE; lost.epoch = r.epoch; lost.frame = old.frame;
    r.addon->hooks()->world_frame_result(&lost); r.result(old); r.finish(old.frame); r.addon->pulse();
    check(r.count(tel::Action::Presented) == 0, "FrameUnavailable left issued render proofs active");
    const auto hello_count = r.count(tel::Action::Hello);
    r.now += 16; r.frame();
    check(r.count(tel::Action::Hello) == hello_count && r.submissions.size() == 2,
          "Skipped draw discarded a same-zone warning or restarted negotiation");
    r.addon->begin_zone(); r.now += 16; r.frame();
    const auto before = r.sent.size();
    check(r.count(tel::Action::Hello) == hello_count, "Retired zone epoch was allowed to advertise a new scene");
    ++r.epoch; r.now += 16; r.frame();
    check(r.sent.size() > before && r.sent.back().action == tel::Action::Hello, "New zone epoch could not reacquire readiness");
    r.result(old); r.finish(old.frame, 6, old.epoch); r.addon->pulse();
    check(r.count(tel::Action::Presented) == 0, "Old-zone receipt certified current zone");
    auto stale = r.context(); stale.epoch = old.epoch; r.addon->hooks()->world_frame(&stale);
    r.ack(); r.incoming(circle(44)); r.now += 16; r.frame();
    check(!r.submissions.empty() && r.submissions.back().epoch == r.epoch, "Stale SDK frame rolled scene back to retired epoch");
    const auto active = r.submissions.back();
    r.announce("other"); r.result(active); r.finish(active.frame); r.addon->pulse();
    check(r.count(tel::Action::Presented) == 0 && r.incoming(circle()) == 0, "World switch retained peer proof/routing state");
    r.announce(); r.now += 16; r.frame(); r.ack(); r.incoming(circle(45)); r.now += 16; r.frame();
    const auto unflushed = r.submissions.back(); r.result(unflushed); r.finish(unflushed.frame);
    r.addon->game_state(GAMESTATE_CHARSELECT); r.addon->pulse();
    check(r.count(tel::Action::Presented) == 0, "Character-select retirement sent a queued proof");
    r.addon->game_state(GAMESTATE_INGAME); r.now += 16; r.frame();
    check(r.sent.back().action == tel::Action::Hello && r.sent.back().revision == 1,
          "Same-epoch return after character-select did not request replacement snapshot");
}

void skipped_draws_keep_current_warnings() {
    for (unsigned client = 0; client < 3; ++client) {
        Rig r;
        r.connect();
        r.incoming(circle());
        r.now += 16;
        r.frame();
        const auto previous = r.submissions.back();
        HailstoneWorldReceipt skipped{};
        skipped.size = sizeof(skipped);
        skipped.status = HAILSTONE_WORLD_FRAME_UNAVAILABLE;
        skipped.epoch = r.epoch;
        skipped.frame = previous.frame;
        r.caps = 0;
        r.addon->hooks()->world_frame_result(&skipped);
        r.result(previous);
        r.finish(previous.frame, 0);
        r.now += 1200;
        r.addon->pulse();
        r.incoming(control(tel::Action::Cancel, 42, 4));
        r.incoming(circle(43, 20, 34));
        r.addon->hooks()->world_frame_result(&skipped);
        check(r.count(tel::Action::Presented) == 0, "A skipped draw certified a warning");
        r.caps = 31;
        r.now += 16;
        r.frame();
        check(r.submissions.size() == 2 && r.count(tel::Action::Hello) == 1,
              "Resume dropped the current Create or retained the Cancel");
        r.present(r.submissions.back());
        check(r.count(tel::Action::Presented) == 1 && r.sent.back().id == 43,
              "Only the replacement's successful draw should certify a warning");

        r.caps = 0;
        r.now += 30001;
        r.addon->pulse();
        check(r.count(tel::Action::Hello) == 2 && r.sent.back().revision == 0,
              "A long background pause let the server lease expire");
        r.now += 1999;
        r.addon->pulse();
        check(r.count(tel::Action::Hello) == 2, "An unacknowledged renewal retried too early");
        ++r.now;
        r.addon->pulse();
        check(r.count(tel::Action::Hello) == 3, "An unacknowledged renewal did not retry after two seconds");
    }
}

void drawn_anchor_boundary() {
    Rig r; r.connect();
    HailstoneWorldAnchor actor{}; actor.size = sizeof(actor); actor.spawn_id = 71;
    actor.flags = HAILSTONE_WORLD_DRAWN_POSE | HAILSTONE_WORLD_CACHED_FLOOR;
    actor.position[0] = 134; actor.position[1] = 112; actor.position[2] = 30; actor.heading = 128; actor.floor_z = 0;
    r.actors.emplace(71, actor);
    r.sample = [](float, float, float probe, float& z) {
        z = probe >= 40 ? 40.0f : 0.0f; return probe >= 0 ? HAILSTONE_WORLD_RESOLVED : HAILSTONE_WORLD_MISSING;
    };
    auto bytes = circle(); bytes[7] = std::byte{1}; bytes[8] = std::byte{0x87}; put(bytes, 28, 71);
    r.incoming(bytes); r.now += 16; r.frame();
    check(r.submissions.size() == 1, "Current drawn actor did not resolve through SDK");
    const auto& vertices = r.submissions[0].vertices;
    check(near(vertices[0].position[0], 134) && near(vertices[0].position[1], 112) && near(vertices[0].position[2], .035f),
          "Drawn pose XY conversion or valid cached-floor reference was lost");
    check(near(vertices[1].position[0], 134) && near(vertices[1].position[1], 120), "SDK drawn heading was swapped or converted twice");
    const auto floor_queries = r.probes.size();
    const auto anchor_queries = r.anchor_queries;
    r.remaining_queries = 1; // Enough to verify the exact current actor pose only.
    r.now += 16; r.frame();
    check(r.submissions.size() == 2 && r.probes.size() == floor_queries && r.anchor_queries == anchor_queries + 1,
          "A stable actor pose could not reuse the certified terrain footprint");
    r.actors[71].position[0] += 2;
    r.now += 16; r.frame();
    check(r.submissions.size() == 2, "Movement reused an old actor footprint");
    r.remaining_queries = 262144;
    r.now += 16; r.frame();
    check(r.submissions.size() == 3 && near(r.submissions.back().vertices[0].position[0], 136),
          "An actor-following warning did not recover at the changed pose");
    const auto duplicate_pose_queries = r.anchor_queries;
    put(bytes, 20, 43);
    r.incoming(bytes); r.now += 16; r.frame();
    check(r.submissions.size() == 5 && r.anchor_queries == duplicate_pose_queries + 1,
          "Warnings following the same actor repeated the acquired-frame pose query");
    r.actors[71].flags = 0; r.now += 16; const auto before = r.submissions.size(); r.frame();
    check(r.submissions.size() == before, "Anchor without actual drawn-pose flag produced a fallback marker");
}
void draping_survives_appearance_choices() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("bastion-tel-drape-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path file, directory;
        ~Cleanup() { std::error_code ec; std::filesystem::remove(file, ec); std::filesystem::remove(directory, ec); }
    } cleanup{directory / "telegraphs.cfg", directory};
    const auto path_bytes = directory.u8string();
    const std::string config_path(reinterpret_cast<const char*>(path_bytes.data()), path_bytes.size());
    for (unsigned appearance = 0; appearance < 4; ++appearance) {
        std::ofstream settings(cleanup.file);
        settings << "unified_colors=" << (appearance & 1) << "\ncontrast_edges=" << ((appearance >> 1) & 1) << '\n';
        settings.close();
        Rig r;
        r.config_path = config_path;
        r.api.character_config_dir = [](void* context, char* out, std::size_t capacity) {
            // Keep the configured path in a fixture-owned string, outside the
            // renderer protocol bytes and independent of palette preferences.
            const auto& path = static_cast<Rig*>(context)->config_path;
            if (!out || capacity <= path.size()) return 0;
            std::memcpy(out, path.c_str(), path.size() + 1); return static_cast<int>(path.size());
        };
        r.adapter.value.command("status");
        r.sample = [](float, float, float probe, float& z) {
            // The ground plane is still present under a low obstacle.
            for (const float height : {40.0f, 4.0f, 0.0f}) if (height <= probe) { z = height; return HAILSTONE_WORLD_RESOLVED; }
            return HAILSTONE_WORLD_MISSING;
        };
        r.connect(); r.incoming(circle()); r.now += 16; r.frame();
        check(r.submissions.size() == 1 && std::all_of(r.submissions.front().vertices.begin(), r.submissions.front().vertices.end(), [](const auto& v) {
            return near(v.position[2], 4.035f);
        }), "server colors or edge toggle disabled connected low-obstacle draping");
    }
}

void failed_world_warning_reaches_hud_without_proof() {
    Rig r;
    r.api.add_ui_function = [](void* context, const char*, HailstoneUiFn fn, void* user) {
        auto& rig = *static_cast<Rig*>(context);
        rig.fallback_ui = fn;
        rig.fallback_ui_context = user;
        return 1;
    };
    r.api.arg = [](void*, const HailstoneArgs*, std::size_t n, HailstoneValue* value) {
        if (n) return 0;
        value->kind = HAILSTONE_STRING;
        value->s = "fallback";
        return 1;
    };
    r.api.out_begin_array = [](void*, HailstoneOut*) {};
    r.api.out_end = [](void*, HailstoneOut*) {};
    r.api.out_begin_table = [](void*, HailstoneOut* out) { ++reinterpret_cast<FallbackOutput*>(out)->tables; };
    r.api.out_key = [](void*, HailstoneOut* out, const char* key) { reinterpret_cast<FallbackOutput*>(out)->key = key; };
    r.api.out_value = [](void*, HailstoneOut* out, const HailstoneValue* value) {
        auto& capture = *reinterpret_cast<FallbackOutput*>(out);
        if (value->kind == HAILSTONE_STRING) capture.text[capture.key] = value->s;
        if (value->kind == HAILSTONE_INT) capture.numbers[capture.key] = value->i;
    };
    r.addon->initialize();
    r.connect();
    auto optional = circle();
    optional[9] = std::byte{0};
    r.incoming(optional);
    r.sample = [](float, float, float, float&) { return HAILSTONE_WORLD_MISSING; };
    r.now += 16; r.frame(); r.finish(r.serial); r.addon->pulse();
    const auto poll = [&]() {
        FallbackOutput capture;
        check(r.fallback_ui && r.fallback_ui(r.fallback_ui_context, reinterpret_cast<const HailstoneArgs*>(&r),
                                             reinterpret_cast<HailstoneOut*>(&capture)),
              "Fallback HUD query was not callable");
        return capture;
    };
    const auto failed = poll();
    check(r.submissions.empty() && r.count(tel::Action::Presented) == 0 && failed.tables == 2 &&
          failed.text.at("reason") == "terrain" && failed.numbers.at("id") == 42 &&
          failed.numbers.at("remaining_ms") > 0 && !failed.numbers.contains("distance"),
          "A valid optional warning without terrain did not reach the HUD, reported camera distance, or earned proof");
    r.incoming(control(tel::Action::Cancel, 42, 4));
    check(poll().tables == 1, "A cancelled warning stayed visible in the HUD before the next world frame");
}

void sustained_warning_under_submission_pressure() {
    Rig r; r.connect(); r.maximum_batches = 1; r.maximum_vertices = 24;
    for (std::uint32_t id = 1; id <= 3; ++id) r.incoming(circle(id, static_cast<float>(id * 20), 0));
    auto optional = circle(4, 400, 0); optional[9] = std::byte{0}; r.incoming(optional);
    for (unsigned frame = 0; frame < 3; ++frame) {
        r.now += 16; const auto before = r.submissions.size(); r.frame();
        check(r.submissions.size() == before + 1, "Tight frame budget did not admit exactly one required marker");
        const auto saved = r.submissions.back();
        check(near(saved.vertices[0].position[1], 20), "An admitted warning blinked out under hard submission pressure");
        r.present(saved);
    }
    check(r.count(tel::Action::Presented) == 1 && r.sent.back().id == 1,
          "An unshown warning earned a presentation acknowledgement");
    r.incoming(control(tel::Action::Cancel, 1, 4));
    r.now += 16; r.frame();
    check(r.submissions.size() == 4 && near(r.submissions.back().vertices[0].position[1], 40),
          "Cancelling the visible warning did not free capacity for another required warning");
}

#ifdef BASTION_SDK_BOUNDED_SURFACE_PATCH
void bounded_surface_work_keeps_later_warnings_visible() {
    Rig r;
    r.caps |= HAILSTONE_WORLD_BOUNDED_SURFACE_PATCH;
    r.api.world_surface_patch_bounded = [](void* context, const HailstoneWorldSurfacePatch* query,
                                          HailstoneWorldSurfacePatchResult* result, std::uint32_t* work) {
        auto& rig = *static_cast<Rig*>(context);
        check(work && *work && *work <= rig.remaining_queries, "Surface query exceeded the host work allowance");
        if (query->vertices[1] < 40) {
            check(*work <= rig.remaining_queries / 2, "First marker was allowed to exhaust other markers' terrain work");
            return static_cast<int>(HAILSTONE_WORLD_PATCH_DEFERRED); // Exhaust the requested work.
        }
        *work = 1;
        return rig.api.world_surface_patch(context, query, result);
    };
    r.remaining_queries = 40;
    r.connect();
    r.incoming(circle(1, 20, 0));
    r.incoming(circle(2, 60, 0));
    r.now += 16; r.frame();
    check(r.submissions.size() == 1 && near(r.submissions[0].vertices[0].position[1], 60),
          "One expensive patch starved a later warning of work");
    r.present(r.submissions[0]);
    check(r.count(tel::Action::Presented) == 1 && r.sent.back().id == 2,
          "Work-starved footprint earned an acknowledgement or blocked the completed warning's proof");
    r.remaining_queries = 0;
    r.now += 16; r.frame();
    check(r.submissions.size() == 2 && near(r.submissions[1].vertices[0].position[1], 60),
          "A cached warning depended on terrain query capacity for every presentation");
}
#endif

void footprint_cache_invalidation_and_decoration_receipts() {
    Rig r; r.connect();
    auto bytes = circle();
    bytes[8] |= std::byte{static_cast<unsigned char>(tel::Countdown)};
    r.incoming(bytes); r.now += 16; r.frame();
    check(r.submissions.size() == 2 && r.submissions[0].vertices.size() == 24,
          "Warning footprint and countdown were not independent submissions");
    r.present(r.submissions[1]);
    check(r.count(tel::Action::Presented) == 0, "Countdown geometry alone earned a warning proof");
    r.maximum_vertices = 24;
    const auto before = r.probes.size();
    r.now += 16; r.frame();
    check(r.submissions.size() == 3 && r.probes.size() == before,
          "A stable footprint did not reuse its terrain proof independently of the clock");
    r.present(r.submissions.back());
    check(r.count(tel::Action::Presented) == 1, "A complete warning without the countdown could not earn a proof");

    ++r.collision_revision;
    r.sample = [](float, float, float, float&) { return HAILSTONE_WORLD_MISSING; };
    r.now += 16; r.frame();
    check(r.submissions.size() == 3 && r.probes.size() > before, "Changed terrain reused an invalidated mesh");
    r.sample = [](float, float, float probe, float& z) { z = 2; return probe >= 2 ? HAILSTONE_WORLD_RESOLVED : HAILSTONE_WORLD_MISSING; };
    r.now += 16; r.frame();
    check(r.submissions.size() == 4 && near(r.submissions.back().vertices[0].position[2], 2.035f),
          "The warning failed to return on restored current terrain");

    bytes[5] = std::byte{static_cast<unsigned char>(tel::Action::Update)};
    put(bytes, 24, 4); floating(bytes, 56, 20);
    r.incoming(bytes); r.now += 16; r.frame();
    check(r.submissions.size() == 5 && near(r.submissions.back().vertices[0].position[1], 20),
          "A same-ID update reused the old footprint");
    r.present(r.submissions.back());
    check(r.count(tel::Action::Presented) == 2 && r.sent.back().revision == 4,
          "An updated footprint lost its exact revision proof");
}

#ifdef BASTION_SDK_SURFACE_CACHE_REVISION
void regional_cache_preserves_static_footprints() {
    Rig r;
    r.caps |= HAILSTONE_WORLD_SURFACE_CACHE_REVISION;
    r.api.world_surface_cache_revision = [](void* context, std::uint64_t frame, std::uint32_t epoch,
                                           float min_x, float min_y, float max_x, float max_y, std::uint32_t* out) -> int {
        auto& rig = *static_cast<Rig*>(context);
        check(out && frame == rig.serial && epoch == rig.epoch, "Regional query lost exact scene identity");
        check(min_x < 26 && min_y < 4 && max_x > 42 && max_y > 20,
              "Regional bounds did not cover the expanded floor proof in SDK axes");
        if (!rig.regional_cache_valid) return HAILSTONE_WORLD_DEFERRED;
        *out = rig.regional_revision;
        return HAILSTONE_WORLD_RESOLVED;
    };
    r.connect(); r.incoming(circle()); r.now += 16; r.frame();
    check(r.submissions.size() == 1, "Static footprint could not be built");
    const auto before = r.probes.size();
    r.remaining_queries = 0; ++r.collision_revision;
    r.now += 16; r.frame();
    check(r.submissions.size() == 2 && r.probes.size() == before,
          "Unrelated animation invalidated an unchanged regional terrain certificate");
    ++r.regional_revision; ++r.collision_revision;
    r.now += 16; r.frame();
    check(r.submissions.size() == 2, "Regional terrain change reused stale geometry");

    r.remaining_queries = 262144;
    r.now += 16; r.frame();
    check(r.submissions.size() == 3, "Stable changed terrain did not rebuild the footprint");
    r.regional_cache_valid = false; ++r.collision_revision; r.remaining_queries = 0;
    r.now += 16; r.frame();
    check(r.submissions.size() == 3, "Region no longer proved static retained a cached warning");
}
#endif

void resume_legacy_surface_work_across_frames() {
    Rig r; r.connect();
    std::uint64_t native_frame = 0;
    unsigned completed_this_frame = 0;
    std::set<std::array<float, 9>> completed_patches;
    r.patch_sample = [&](const HailstoneWorldSurfacePatch& query, HailstoneWorldSurfacePatchResult&) {
        float lo = query.vertices[0], hi = lo;
        for (std::size_t i = 1; i < 3; ++i) {
            lo = std::min(lo, query.vertices[i * 3]); hi = std::max(hi, query.vertices[i * 3]);
        }
        // Force per-triangle proofs. A few native calls fit each frame but the
        // entire warning's proof cannot complete within one frame allowance.
        if (hi - lo > 10) return HAILSTONE_WORLD_PATCH_DEFERRED;
        if (native_frame != query.frame) { native_frame = query.frame; completed_this_frame = 0; }
        if (completed_this_frame == 2) return HAILSTONE_WORLD_PATCH_DEFERRED;
        ++completed_this_frame;
        std::array<float, 9> key{};
        std::copy(std::begin(query.vertices), std::end(query.vertices), key.begin());
        check(completed_patches.insert(key).second, "Native host repeated a completed surface query during replay");
        return HAILSTONE_WORLD_PATCH_PLANAR;
    };
    r.incoming(circle());
    r.now += 16; r.frame();
    check(r.submissions.empty() && completed_patches.size() == 2 && r.count(tel::Action::Presented) == 0,
          "A partial legacy terrain proof was submitted or acknowledged");
    for (int i = 0; i < 6 && r.submissions.empty(); ++i) { r.now += 16; r.frame(); }
    check(r.submissions.size() == 1 && completed_patches.size() > 2,
          "Partial legacy terrain proof restarted instead of finishing across frames");
    r.present(r.submissions.back());
    check(r.count(tel::Action::Presented) == 1, "Completed legacy terrain replay did not qualify for an exact proof");
}

void cached_warning_preserves_server_pulse() {
    Rig r; r.connect();
    auto bytes = circle();
    bytes[8] |= std::byte{static_cast<unsigned char>(tel::Pulse)};
    r.incoming(bytes); r.now += 16; r.frame();
    check(r.submissions.size() == 1, "Pulsing warning did not submit without a countdown");
    const auto probes = r.probes.size();
    r.remaining_queries = 0; r.now += 250; r.frame();
    check(r.submissions.size() == 2 && r.probes.size() == probes, "Pulse animation rebuilt its certified footprint");
    const auto& first = r.submissions[0].vertices;
    const auto& second = r.submissions[1].vertices;
    check(first.size() == second.size() && !near(first[0].color[3], second[0].color[3]),
          "Server pulse did not vary the cached mesh's opacity");
    for (std::size_t n = 0; n < first.size(); ++n) {
        check(std::equal(first[n].position, first[n].position + 3, second[n].position) &&
              first[n].color[3] >= 128.0f / 255.0f && second[n].color[3] >= 128.0f / 255.0f,
              "Pulse moved certified geometry or went below the required opacity floor");
    }
}

void stationary_clock_reuses_valid_terrain_proofs() {
    Rig r; r.connect();
    unsigned patches = 0;
    r.patch_sample = [&](const auto&, auto&) { ++patches; return HAILSTONE_WORLD_PATCH_PLANAR; };
    auto bytes = circle();
    bytes[8] |= std::byte{static_cast<unsigned char>(tel::Countdown)};
    r.incoming(bytes); r.now += 16; r.frame();
    check(r.submissions.size() == 2 && patches > 0, "Clock proof fixture did not emit both layers");
    const auto native_patches = patches;
    const auto probes = r.probes.size();
    r.remaining_queries = 0; r.now += 20; r.frame();
    check(r.submissions.size() == 4 && patches == native_patches && r.probes.size() == probes,
          "A stationary clock could not reuse the certified whole patch at a later countdown phase");
    ++r.collision_revision; r.now += 16; r.frame();
    check(r.submissions.size() == 4, "A stationary clock survived an invalidated terrain certificate");
}
void modern_shared_floor_allowance() {
    Rig r; r.connect();
    r.sample=[](float,float,float probe,float& z) {
        for(float layer:{60.0f,50.0f,40.0f,30.0f,20.0f,10.0f,0.0f}) if(layer<=probe) {z=layer;return HAILSTONE_WORLD_RESOLVED;}
        return HAILSTONE_WORLD_MISSING;
    };
    for (std::uint32_t id = 1; id <= 200; ++id) r.incoming(circle(id, static_cast<float>(id * 20), 0));
    r.now += 16; r.frame();
    check(r.submissions.size() == 200 && r.probes.size() > 1536 && r.probes.size() <= 262144,
          "Owned native 1536-probe frame cap silently limited the modern SDK allowance");
    for (const auto& saved : r.submissions) r.result(saved);
    r.finish(r.serial); r.addon->pulse();
    check(r.count(tel::Action::Presented) == 200, "Multiple admitted complete markers lost final presentation proofs");
}
void invalid_scene_and_clock_guards() {
    for (unsigned invalid = 0; invalid < 9; ++invalid) {
        Rig r; r.announce(); r.addon->game_state(GAMESTATE_INGAME); auto frame = r.context();
        switch (invalid) {
        case 0: frame.size = sizeof(frame) - 1; break;
        case 1: frame.capabilities &= ~static_cast<std::uint32_t>(HAILSTONE_WORLD_TERRAIN); break;
        case 2: frame.epoch = 0; break;
        case 3: frame.frame = 0; break;
        case 4: frame.viewport[2] = 0; break;
        case 5: frame.now_ms = r.now + 1001; break;
        case 6: frame.view_projection[0] = std::numeric_limits<float>::quiet_NaN(); break;
        case 7: frame.maximum_vertices = 0; break;
        default: r.caps = 0; break;
        }
        r.addon->hooks()->world_frame(&frame); r.addon->pulse();
        check(r.sent.empty() && r.submissions.empty(), "Invalid/stale SDK scene advertised TEL1 renderer readiness");
    }
    Rig r; r.connect(); r.incoming(circle()); r.now += 16; r.frame();
    const auto saved = r.submissions.back();
    --r.now; r.addon->pulse(); r.result(saved); r.finish(saved.frame); r.addon->pulse();
    check(r.count(tel::Action::Presented) == 0, "Monotonic clock rollback retained a pending proof");
    r.now += 32; r.frame();
    check(r.sent.back().action == tel::Action::Hello && r.sent.back().revision == 1, "Clock recovery did not replace abandoned marker state");
    r.ack();
    // Complete the requested empty snapshot before admitting a new live marker.
    r.incoming(control(tel::Action::SnapshotBegin)); r.incoming(control(tel::Action::SnapshotEnd));
    r.incoming(circle(43)); r.now += 16; r.frame();
    const auto current = r.submissions.back();
    HailstoneWorldReceipt stale{}; stale.size = sizeof(stale); stale.status = HAILSTONE_WORLD_FRAME_UNAVAILABLE;
    stale.frame = current.frame; stale.epoch = current.epoch - 1;
    r.addon->hooks()->world_frame_result(&stale); r.present(current);
    check(r.count(tel::Action::Presented) == 1 && r.sent.back().id == 43, "Stale unavailable event retired a current scene proof");
}
} // namespace
int main() {
    try {
        actual_adapter_flow_and_axes(); absent_services_and_routing(); receipt_failure_paths(); lifecycle_and_runtime_capability();
        skipped_draws_keep_current_warnings();
        drawn_anchor_boundary(); failed_world_warning_reaches_hud_without_proof(); draping_survives_appearance_choices(); malformed_patch_results(); near_collinear_host_region();
        sustained_warning_under_submission_pressure();
#ifdef BASTION_SDK_BOUNDED_SURFACE_PATCH
        bounded_surface_work_keeps_later_warnings_visible();
#endif
        footprint_cache_invalidation_and_decoration_receipts();
#ifdef BASTION_SDK_SURFACE_CACHE_REVISION
        regional_cache_preserves_static_footprints();
#endif
        resume_legacy_surface_work_across_frames(); cached_warning_preserves_server_pulse();
        stationary_clock_reuses_valid_terrain_proofs();
        modern_shared_floor_allowance(); invalid_scene_and_clock_guards();
        std::cout << "Telegraph adapter: " << checks << " checks passed\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Telegraph adapter after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
