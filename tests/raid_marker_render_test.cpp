#include "raid_marker_render.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using bastion::raid_markers::draw_world;
using bastion::raid_markers::VisualMarker;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Batch {
    std::uint64_t tag;
    std::vector<HailstoneWorldVertex> vertices;
};

struct Rig {
    PluginHostApi host{};
    HailstoneWorldFrame frame{};
    std::vector<Batch> batches;
    unsigned queries = 0;
    bool floor_available = true;

    Rig() {
        frame.size = sizeof(frame);
        frame.epoch = 2;
        frame.frame = 11;
        frame.capabilities = HAILSTONE_WORLD_GEOMETRY | HAILSTONE_WORLD_TERRAIN;
        frame.maximum_batches = 9;
        frame.maximum_vertices = 1600;
        frame.remaining_queries = 360;
        frame.camera_position[1] = -50;
        host.size = sizeof(host);
        host.host = this;
        host.world_floor_below = [](void* context, std::uint64_t token, std::uint32_t epoch,
                                    float x, float, float seed, float* out) {
            auto& self = *static_cast<Rig*>(context);
            check(token == self.frame.frame && epoch == self.frame.epoch, "Terrain probe escaped the current world frame");
            ++self.queries;
            if (!self.floor_available) return static_cast<int>(HAILSTONE_WORLD_MISSING);
            *out = seed - 1.6f + .2f * std::sin(x);
            return static_cast<int>(HAILSTONE_WORLD_RESOLVED);
        };
        host.world_submit = [](void* context, const HailstoneWorldSubmission* value) {
            auto& self = *static_cast<Rig*>(context);
            check(value->frame == self.frame.frame && value->epoch == self.frame.epoch && value->tag,
                  "Submission escaped the current frame or lost its identity");
            check(value->primitive == HAILSTONE_WORLD_TRIANGLES && value->vertex_count && value->vertex_count % 3 == 0,
                  "Submission is missing its triangle mesh");
            for (std::size_t i = 0; i < value->vertex_count; ++i) {
                const auto& vertex = value->vertices[i];
                for (float position : vertex.position) check(std::isfinite(position), "Submitted a nonfinite position");
                for (float color : vertex.color) check(color >= 0 && color <= 1, "Submitted an invalid color");
            }
            self.batches.push_back({value->tag, {value->vertices, value->vertices + value->vertex_count}});
            return 1;
        };
    }

    void reset() { queries = 0; batches.clear(); }
};
} // namespace

int main() try {
    Rig rig;
    const std::array<VisualMarker, 11> placed{{
        {1, 3, 4, 10}, {2, 6, 4, 10}, {3, 9, 4, 10}, {4, 12, 4, 10},
        {5, 15, 4, 10}, {6, 18, 4, 10}, {7, 21, 4, 10}, {8, 24, 4, 10},
        {8, 27, 4, 10}, {9, 1, 0, 0}, {7, std::numeric_limits<float>::infinity(), 0, 0},
    }};
    const VisualMarker preview{1, -8, 4, 10};
    draw_world(rig.host, rig.frame, placed, preview);
    check(rig.batches.size() == 9 && rig.queries == 360, "Expected eight distinct placed types and one preview");
    check(rig.batches[0].tag != rig.batches[1].tag && rig.batches[0].vertices.size() == 84,
          "Preview must have its own identity and a dashed ring");
    check(rig.batches[1].vertices.size() == 156, "Placed Sunburst is missing its full ring, beam, or glyph");
    const auto& ring_vertex = rig.batches[1].vertices.back();
    check(std::abs(ring_vertex.position[2] - (10.09f + .2f * std::sin(ring_vertex.position[0]))) < .0001f,
          "Ground ring missed the sampled floor");
    for (std::size_t i = 2; i < rig.batches.size(); ++i) {
        const auto* previous = rig.batches[i - 1].vertices.front().color;
        const auto* current = rig.batches[i].vertices.front().color;
        check(!std::equal(previous, previous + 3, current), "Consecutive marker types share the same color");
    }

    rig.reset();
    rig.floor_available = false;
    draw_world(rig.host, rig.frame, std::span(placed).first(1), std::nullopt);
    check(rig.batches.size() == 1 && rig.batches[0].vertices.size() == 36,
          "Missing floor must leave a glyph and beam with no fabricated ring");

    rig.reset();
    rig.frame.maximum_batches = 1;
    rig.frame.maximum_vertices = 24;
    draw_world(rig.host, rig.frame, placed, preview);
    check(rig.batches.size() == 1 && rig.batches[0].vertices.size() == 24 && rig.queries == 0,
          "Low geometry allowance must prioritize the preview glyph and skip terrain work");

    rig.reset();
    rig.frame.epoch = 0;
    draw_world(rig.host, rig.frame, placed, preview);
    check(rig.batches.empty() && !rig.queries, "A retired frame must do no work");

    std::cout << "Raid marker render checks passed.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
