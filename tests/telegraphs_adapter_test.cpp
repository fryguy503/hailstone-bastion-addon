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
    bool admission = true, wire_success = true;
    std::vector<tel::Packet> sent;
    std::vector<Saved> submissions;
    std::vector<Probe> probes;
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
    check(r.count(tel::Action::Hello) == hello_count + 1 && r.sent.back().revision == 1, "Renderer restoration did not request replacement snapshot");
    r.ack();
    r.addon->begin_zone(); r.now += 16; r.frame();
    const auto before = r.sent.size();
    check(r.count(tel::Action::Hello) == hello_count + 1, "Retired zone epoch was allowed to advertise a new scene");
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
    r.now += 1001; const auto sent = r.sent.size(); r.addon->pulse();
    check(r.sent.size() == sent, "Stale rendered-frame availability advertised Hello");
    r.now += 16; r.frame();
    check(r.sent.back().action == tel::Action::Hello, "Fresh scene could not restore runtime availability");
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

void bounded_fair_multiple_markers() {
    Rig r; r.connect(); r.maximum_batches = 1; r.maximum_vertices = 24;
    for (std::uint32_t id = 1; id <= 3; ++id) r.incoming(circle(id, static_cast<float>(id * 20), 0));
    auto optional = circle(4, 400, 0); optional[9] = std::byte{0}; r.incoming(optional);
    std::vector<std::uint32_t> acknowledgements;
    for (unsigned frame = 0; frame < 3; ++frame) {
        r.now += 16; const auto before = r.submissions.size(); r.frame();
        check(r.submissions.size() == before + 1, "Tight frame budget did not admit exactly one required marker");
        const auto saved = r.submissions.back();
        check(saved.vertices[0].position[1] < 400, "Optional marker ran ahead of a required marker");
        r.present(saved);
    }
    for (const auto& packet : r.sent) if (packet.action == tel::Action::Presented) acknowledgements.push_back(packet.id);
    std::sort(acknowledgements.begin(), acknowledgements.end());
    check(acknowledgements == std::vector<std::uint32_t>({1, 2, 3}), "Fixed ordering permanently starved a later required marker");
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
        drawn_anchor_boundary(); draping_survives_appearance_choices(); malformed_patch_results(); bounded_fair_multiple_markers(); modern_shared_floor_allowance(); invalid_scene_and_clock_guards();
        std::cout << "Telegraph adapter: " << checks << " checks passed\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Telegraph adapter after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
