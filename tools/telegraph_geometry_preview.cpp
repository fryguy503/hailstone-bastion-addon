// SPDX-License-Identifier: MIT
// Export production CPU geometry at the SDK boundary for a native GPU preview.
#include "telegraphs_geometry.h"
#include "telegraph_surface_fixture.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <array>
#include <algorithm>
#include <cmath>
#include <string_view>
namespace tel = bastion::telegraphs;
class FlatStage final : public tel::GeometryQueries {
    tel::FloorSample floor(float, float, float probe) override {
        return probe >= 0 ? tel::FloorSample{tel::SampleStatus::Resolved, 0} : tel::FloorSample{tel::SampleStatus::Missing};
    }
    tel::DrawnAnchor drawn_anchor(std::uint32_t) override { return {tel::SampleStatus::Missing}; }
    tel::SurfacePatchSample surface_patch(const std::array<tel::TriangleVertex,3>&,float) override {return {tel::PatchStatus::Planar};}
};
// Raw TEL coordinates. Kept analytical and exported as the same explicit
// triangles, so the host GPU probe can depth-test actual production draping.
float base(float x, float y) { return .08f * x + .025f * y; }
class TerrainStage final : public tel::GeometryQueries {
    std::vector<tel::fixture::Region> regions_=[] {
        auto regions=tel::fixture::raised_props({{tel::fixture::rectangle(-22,-16,-4,4),0,0,3}});
        std::vector<tel::fixture::Region> clipped;
        for(auto& region:regions) {
            region.polygon=tel::fixture::intersection(std::move(region.polygon),tel::fixture::rectangle(-34,0,-16,16));
            region.x_slope=.08;region.y_slope=.025;
            if(tel::fixture::area(region.polygon)>1e-10) clipped.push_back(std::move(region));
        }
        for(unsigned n=0;n<4;++n) clipped.emplace_back(tel::fixture::rectangle(n*4,n==3?24:(n+1)*4,-16,16),.08,.025,(n+1)*4);
        return clipped;
    }();
    tel::FloorSample floor(float x, float y, float probe) override {
        if (x < -34 || x > 24 || y < -16 || y > 16) return {tel::SampleStatus::Missing};
        const float land = base(x, y);
        const float stair = x < 0 ? land : land + std::min(16.0f, (std::floor(x / 4) + 1) * 4);
        const float box = x >= -22 && x <= -16 && y >= -4 && y <= 4 ? land + 3 : land;
        if (x >= 8 && land + 42 <= probe) return {tel::SampleStatus::Resolved, land + 42};
        for (const float z : {std::max(stair, box), land})
            if (z <= probe) return {tel::SampleStatus::Resolved, z};
        return {tel::SampleStatus::Missing};
    }
    tel::DrawnAnchor drawn_anchor(std::uint32_t) override { return {tel::SampleStatus::Missing}; }
    tel::SurfacePatchSample surface_patch(const std::array<tel::TriangleVertex,3>& vertices,float rise) override {return tel::fixture::certify(vertices,regions_,rise);}
};
void write_vertex(std::ostream& file, const tel::TriangleVertex& v) {
    file << v.y << ' ' << v.x << ' ' << v.z << ' '
         << static_cast<float>((v.argb >> 16) & 255) / 255 << ' '
         << static_cast<float>((v.argb >> 8) & 255) / 255 << ' '
         << static_cast<float>(v.argb & 255) / 255 << ' '
         << static_cast<float>(v.argb >> 24) / 255 << '\n';
}
void surface_quad(std::ostream& file, const std::array<tel::TriangleVertex, 4>& corners) {
    for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u}) write_vertex(file, corners[index]);
}
void top(std::ostream& file, float left, float right, float back, float front, float rise, std::uint32_t color) {
    surface_quad(file, {{{left, back, base(left, back) + rise, color}, {right, back, base(right, back) + rise, color},
                         {right, front, base(right, front) + rise, color}, {left, front, base(left, front) + rise, color}}});
}
void terrain_fixture(std::ostream& file) {
    top(file, -34, 24, -16, 16, 0, 0xff344353u);
    for (unsigned n = 0; n < 4; ++n) {
        const float left = static_cast<float>(n) * 4, right = n == 3 ? 24 : left + 4;
        const float rise = static_cast<float>(n + 1) * 4;
        top(file, left, right, -16, 16, rise, n % 2 ? 0xff536477u : 0xff495c70u);
        surface_quad(file, {{{left, -16, base(left, -16) + rise - 4, 0xff273443u},
                             {left, 16, base(left, 16) + rise - 4, 0xff273443u},
                             {left, 16, base(left, 16) + rise, 0xff273443u},
                             {left, -16, base(left, -16) + rise, 0xff273443u}}});
    }
    top(file, -22, -16, -4, 4, 3, 0xff607387u);
    const std::array<std::pair<float,float>, 4> corners = {{{-22.0f,-4.0f},{-16.0f,-4.0f},{-16.0f,4.0f},{-22.0f,4.0f}}};
    for (unsigned n = 0; n < 4; ++n) {
        const auto a = corners[n], b = corners[(n + 1) % 4];
        surface_quad(file, {{{a.first,a.second,base(a.first,a.second),0xff2d3d4du},
                             {b.first,b.second,base(b.first,b.second),0xff2d3d4du},
                             {b.first,b.second,base(b.first,b.second)+3,0xff2d3d4du},
                             {a.first,a.second,base(a.first,a.second)+3,0xff2d3d4du}}});
    }
}
int main(int argc, char **argv) {
    const bool terrain = argc == 4 && std::string_view(argv[2]) == "--terrain";
    if (argc != 2 && !terrain) { std::cerr << "Usage: bastion_telegraph_geometry_preview <vertices> [--terrain <surface-vertices>]\n"; return 1; }
    std::ofstream file(argv[1]);
    if (!file) return 1;
    file << std::setprecision(9);
    FlatStage stage;
    TerrainStage terrain_stage;
    tel::GeometryOptions options;
    options.unified_palette = options.enhanced_style = options.bounded_draping = true;
    options.ribbon_width = .32f;
    options.hostile = 0x9cf06b63; options.friendly = 0x944fa9f5; options.neutral = 0x9ce6bd67; options.safe = 0x944ed6aa;
    const unsigned count = terrain ? 1u : 11u;
    tel::GeometryBudget budget; budget.vertex_limit = 98304; budget.required_markers_remaining = count;
    for (unsigned shape = 1; shape <= count; ++shape) {
        tel::Packet packet; packet.id = shape; packet.revision = 1; packet.shape = static_cast<tel::Shape>(shape);
        packet.action = tel::Action::Create;
        packet.flags = tel::Filled | tel::Outline | tel::Countdown | tel::Pulse | tel::ClampToTerrain | tel::Directional | tel::RequiredDisplay;
        packet.disposition = static_cast<tel::Disposition>((shape - 1) % 4 + 1);
        packet.style = tel::Style::Gradient;
        packet.x = -static_cast<float>((shape - 1) / 4) * 42;
        packet.y = static_cast<float>((shape - 1) % 4) * 42;
        packet.p1 = 12; packet.p2 = 6; packet.p4 = 48;
        if (packet.shape == tel::Shape::Cone) packet.p2 = 80;
        if (packet.shape == tel::Shape::Line) { packet.p1 = 24; packet.p2 = 3; }
        if (packet.shape == tel::Shape::Rectangle) { packet.p1 = 22; packet.p2 = 12; }
        if (packet.shape == tel::Shape::Cross) { packet.p1 = 23; packet.p2 = 5; }
        if (terrain) {
            packet.shape = tel::Shape::Rectangle; packet.x = -6; packet.y = 0; packet.z = base(-6, 0);
            packet.p1 = 24; packet.p2 = 52;
        }
        packet.start_ms = packet.server_now_ms = 100; packet.impact_ms = 4100; packet.expire_ms = 5100;
        tel::RenderMarker marker{packet, {1, 0, shape, 1, 1}, 100, 4100, 5100};
        auto& queries = terrain ? static_cast<tel::GeometryQueries&>(terrain_stage) : static_cast<tel::GeometryQueries&>(stage);
        const auto geometry = tel::build_geometry(marker, 2100, queries, budget, options);
        --budget.required_markers_remaining;
        if (!geometry.complete()) { std::cerr << "Incomplete shape " << shape << '\n'; return 1; }
        for (const auto &v : geometry.vertices) write_vertex(file, v);
    }
    file.close();
    if (terrain) {
        std::ofstream surfaces(argv[3]); surfaces << std::setprecision(9); terrain_fixture(surfaces);
        if (!surfaces) return 1;
    }
    std::cout << budget.vertices_used << " production SDK vertices; " << budget.floor_queries_used << " surface queries\n";
    return file ? 0 : 1;
}
