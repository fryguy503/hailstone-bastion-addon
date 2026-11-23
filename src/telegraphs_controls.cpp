#include "telegraphs_adapter.h"
#include "citadel_services.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>

namespace bastion::telegraphs {
namespace {
constexpr Preference definitions[] = {
    {"enabled",0,1,1}, {"opacity",0,100,100},
    {"unified_colors",0,1,1}, {"contrast_edges",0,1,1}, {"edge_width",10,100,32},
    {"hostile",0,0xffffffff,0x9cf06b63}, {"friendly",0,0xffffffff,0x944fa9f5},
    {"neutral",0,0xffffffff,0x9ce6bd67}, {"safe",0,0xffffffff,0x944ed6aa}
};
constexpr std::string_view shapes[] = {"none","circle","donut","cone","rectangle","line","cross","stack","spread","moveaway","gaze","target"};
constexpr std::string_view dispositions[] = {"auto","hostile","friendly","neutral","safe"};
constexpr std::string_view styles[] = {"auto","solid","striped","checker","gradient","ring"};
template<std::size_t N> int index(std::string_view name, const std::string_view (&names)[N]) {
    for (std::size_t n = 0; n < N; ++n) if (name == names[n]) return static_cast<int>(n);
    return -1;
}
}
std::span<const Preference> preferences() { return definitions; }
void Adapter::initialize() {
    if (HAILSTONE_HOST_HAS(host_, add_ui_function)) host_->add_ui_function(host_->host,"telegraphs",ui_callback,this);
}
void Adapter::report(std::string_view text) {
    message_ = std::string(text);
    if (HAILSTONE_HOST_HAS(host_, write_chat)) host_->write_chat(host_->host, message_.c_str(), 0);
}
GeometryOptions Adapter::geometry_options() const {
    GeometryOptions out;
    out.unified_palette = preferences_.get("unified_colors") != 0;
    out.enhanced_style = preferences_.get("contrast_edges") != 0;
    out.bounded_draping = true;
    out.ribbon_width = static_cast<float>(preferences_.get("edge_width")) / 100.0f;
    out.opacity_percent = static_cast<unsigned>(preferences_.get("opacity"));
    out.hostile = static_cast<std::uint32_t>(preferences_.get("hostile"));
    out.friendly = static_cast<std::uint32_t>(preferences_.get("friendly"));
    out.neutral = static_cast<std::uint32_t>(preferences_.get("neutral"));
    out.safe = static_cast<std::uint32_t>(preferences_.get("safe"));
    return out;
}
bool Adapter::test(std::string_view arguments) {
    if (!selected_ || !in_zone_ || !HAILSTONE_HOST_HAS(host_, get_player) || !HAILSTONE_HOST_HAS(host_, get_spawn) ||
        !HAILSTONE_HOST_HAS(host_, monotonic_ms)) { report("Enter the world before testing telegraphs."); return false; }
    HailstonePlayer player{}; player.size = sizeof(player);
    HailstoneSpawn actor{}; actor.size = sizeof(actor);
    if (!host_->get_player(host_->host,&player) || !player.in_zone || !host_->get_spawn(host_->host,player.spawn_id,&actor) ||
        !actor.incarnation) { report("The current character is unavailable."); return false; }
    std::istringstream stream{std::string(arguments)};
    std::string shape, disposition, style, arrows, extra;
    stream >> shape >> disposition >> style >> arrows >> extra;
    if (shape.empty()) shape = "circle";
    if (shape == "move_away") shape = "moveaway";
    const bool all = shape == "all", palette = shape == "palette";
    if (all || palette) { arrows = disposition; disposition = "hostile"; style = "solid"; }
    if (disposition.empty()) disposition = "hostile";
    if (style.empty()) style = "solid";
    const auto s = index(shape,shapes), d = index(disposition,dispositions), pattern = index(style,styles);
    if ((!all && !palette && s < 1) || d < 0 || pattern < 0 || !extra.empty() ||
        (!arrows.empty() && arrows != "arrows" && arrows != "noarrows")) {
        report("Usage: /telegraphs test <shape|all|palette> [disposition] [style] [arrows|noarrows]"); return false;
    }
    const auto now = static_cast<std::uint32_t>(host_->monotonic_ms(host_->host));
    local_player_ = player.spawn_id; local_zone_ = player.zone_id; local_owner_ = actor.incarnation;
    local_.clear();
    const unsigned count = all ? 11u : palette ? 4u : 1u;
    for (unsigned n = 0; n < count; ++n) {
        Packet p; p.id = n + 1; p.revision = 1;
        p.shape = static_cast<Shape>(all ? n + 1 : palette ? 1 : static_cast<unsigned>(s));
        p.disposition = static_cast<Disposition>(palette ? n + 1 : static_cast<unsigned>(d));
        p.style = static_cast<Style>(all ? (n % 5) + 1 : static_cast<unsigned>(pattern));
        p.flags = Filled | Outline | Pulse | Countdown | ClampToTerrain;
        if (arrows == "arrows") p.flags |= Directional;
        // SDK zone-file axes are swapped exactly once to TEL/server axes.
        p.x = actor.y; p.y = actor.x; p.z = actor.z; p.heading = actor.heading;
        if (n) {
            const auto angle = actor.heading * (6.28318530718f / 512.0f) +
                static_cast<float>(n - 1) * 6.28318530718f / static_cast<float>(count - 1);
            p.x += std::sin(angle) * 42; p.y += std::cos(angle) * 42;
        }
        p.server_now_ms = p.start_ms = now; p.impact_ms = now + 11000; p.expire_ms = now + 12000;
        p.p1 = 12;
        if (p.shape == Shape::Donut) p.p2 = 6;
        if (p.shape == Shape::Cone) { p.p1 = 18; p.p2 = 70; }
        if (p.shape == Shape::Rectangle || p.shape == Shape::Line || p.shape == Shape::Cross) {
            p.p1 = 20; p.p2 = p.shape == Shape::Line ? 2.0f : 6.0f;
        }
        RenderMarker marker; marker.packet = p;
        marker.local_start = now; marker.local_impact = p.impact_ms; marker.local_expire = p.expire_ms;
        marker.tag = {epoch_ ? epoch_ : 1u,p.generation,p.id,p.revision,++local_serial_};
        if (valid(p)) local_.push_back(marker);
    }
    report("Local telegraph preview: " + std::to_string(local_.size()) + " markers for 12 seconds.");
    return !local_.empty();
}
void Adapter::draw_local(const HailstoneWorldFrame& frame, std::uint32_t now, GeometryQueries& queries,
                         GeometryBudget& budget, std::size_t remaining_batches) {
    if (!local_.empty()) {
        HailstonePlayer player{}; player.size = sizeof(player); HailstoneSpawn actor{}; actor.size = sizeof(actor);
        if (!HAILSTONE_HOST_HAS(host_,get_player) || !HAILSTONE_HOST_HAS(host_,get_spawn) ||
            !host_->get_player(host_->host,&player) || !player.in_zone || player.spawn_id!=local_player_ || player.zone_id!=local_zone_ ||
            !host_->get_spawn(host_->host,player.spawn_id,&actor) || actor.incarnation!=local_owner_) local_.clear();
    }
    std::erase_if(local_, [&](const auto& marker) { return reached(now,marker.local_expire); });
    local_submitted_ = 0;
    if (!preferences_.get("enabled")) return;
    for (const auto& marker : local_) {
        if (local_submitted_ >= remaining_batches) break;
        auto current = marker; current.tag.epoch = frame.epoch;
        const auto drawn_vertices = budget.vertices_used;
        auto geometry = build_geometry(current,now,queries,budget,geometry_options());
        if (!geometry.complete()) continue;
        std::vector<HailstoneWorldVertex> vertices;
        vertices.reserve(geometry.vertices.size());
        for (const auto& vertex : geometry.vertices) {
            HailstoneWorldVertex out{};
            out.position[0] = vertex.y; out.position[1] = vertex.x; out.position[2] = vertex.z;
            out.color[0] = static_cast<float>((vertex.argb >> 16) & 255) / 255;
            out.color[1] = static_cast<float>((vertex.argb >> 8) & 255) / 255;
            out.color[2] = static_cast<float>(vertex.argb & 255) / 255;
            out.color[3] = static_cast<float>(vertex.argb >> 24) / 255;
            vertices.push_back(out);
        }
        if (submit(frame, marker.tag.incarnation, vertices)) ++local_submitted_;
        else budget.vertices_used = drawn_vertices;
    }
}
bool Adapter::command(std::string_view arguments) {
    preferences_.refresh(host_);
    std::istringstream stream{std::string(arguments)};
    std::string operation, key, value, extra; stream >> operation;
    if (operation=="selftest") {
        class Flat final : public GeometryQueries {
            FloorSample floor(float,float,float) override { return {SampleStatus::Resolved,0}; }
            DrawnAnchor drawn_anchor(std::uint32_t) override { return {SampleStatus::Missing}; }
        } queries;
        bool okay=true;
        for (unsigned shape=1;shape<=11;++shape) {
            Packet p;p.id=shape;p.revision=1;p.shape=static_cast<Shape>(shape);p.flags=Filled|Outline|ClampToTerrain;
            p.start_ms=p.server_now_ms=100;p.impact_ms=1100;p.expire_ms=2100;p.p1=12;p.p2=6;
            if(p.shape==Shape::Cone)p.p2=70;
            RenderMarker marker{p,{1,0,shape,1,1},100,1100,2100};GeometryBudget budget;
            okay=build_geometry(marker,200,queries,budget).complete()&&okay;
        }
        report(okay?"Local CPU shape self-test passed. Use /telegraphs test all for an on-screen preview.":"Local CPU shape self-test failed.");
        return true;
    }
    if (operation.empty() || operation == "status") {
        report("Telegraphs: " + std::string(session_.ready() ? "server ready" : "waiting for server/renderer") +
            "; active " + std::to_string(session_.active_count()) + "; local previews " + std::to_string(local_.size()) +
            "; cached footprints drawn " + std::to_string(reused_footprints_) +
            "; required footprints deferred " + std::to_string(deferred_warning_footprints_) +
            "; opacity " + std::to_string(preferences_.get("opacity")) + "%."); return true;
    }
    if (operation == "test") {
        std::string rest; std::getline(stream,rest); test(rest); return true;
    }
    if (operation == "clear") { local_.clear(); session_.request_snapshot(); report("Local previews cleared; server snapshot requested."); return true; }
    if (operation == "hello") { session_.request_snapshot(); report("Telegraph resynchronization requested."); return true; }
    if (operation == "reload") { preferences_.reload(); report(preferences_.error().empty() ? "Telegraph settings reloaded." : preferences_.error()); return true; }
    if (operation == "on" || operation == "off") {
        const auto okay = preferences_.set("enabled", operation == "on" ? 1 : 0);
        report(okay ? "Telegraph preference saved. Required encounter markers remain visible." : preferences_.error()); return true;
    }
    stream >> key >> value >> extra;
    std::int64_t number = 0;
    if (operation == "opacity" && value.empty() && extra.empty()) {
        const auto parsed = std::from_chars(key.data(),key.data()+key.size(),number);
        if (parsed.ec == std::errc{} && parsed.ptr == key.data()+key.size() && preferences_.set("opacity",number)) {
            report("Telegraph opacity saved."); return true;
        }
    }
    if (operation == "color" && index(key,dispositions) > 0 && !value.empty() && extra.empty()) {
        auto digits = std::string_view(value); if (digits.starts_with("0x")) digits.remove_prefix(2); if (digits.starts_with("#")) digits.remove_prefix(1);
        const auto parsed = std::from_chars(digits.data(),digits.data()+digits.size(),number,16);
        if ((digits.size()==6 || digits.size()==8) && parsed.ec == std::errc{} && parsed.ptr == digits.data()+digits.size()) {
            if (digits.size()==6) number |= 0xa8000000;
            if (preferences_.set(key,number)) { report("Telegraph palette color saved."); return true; }
        }
    }
    report("/telegraphs status|on|off|opacity <0-100>|color <role> <AARRGGBB>|reload|hello|clear|selftest|test <shape|all|palette>");
    return true;
}
int Adapter::ui_callback(void* self,const HailstoneArgs* args,HailstoneOut* out) noexcept {
    if (!self) return 0;
    auto &owner = *static_cast<Adapter*>(self);
    return services::guarded_ui(owner.host_, [&] { return owner.ui(args,out); });
}
int Adapter::ui(const HailstoneArgs* args,HailstoneOut* out) {
    services::Output result{host_,out}; if (!result.valid()) return 0;
    const auto op = services::argument(host_,args,0,16); if (!op) return 0;
    preferences_.refresh(host_);
    if (*op == "command") { const auto line = services::argument(host_,args,1,192); if (line) command(*line); }
    else if (*op == "set") {
        const auto key = services::argument(host_,args,1,16); HailstoneValue value{};
        if (key && host_->arg(host_->host,args,2,&value) && value.kind == HAILSTONE_INT) preferences_.set(*key,value.i);
    } else if (*op == "preset") {
        const auto name = services::argument(host_,args,1,16);
        if (name && (*name == "bastion" || *name == "contrast")) {
            const bool contrast = *name == "contrast";
            preferences_.set("unified_colors",1); preferences_.set("contrast_edges",1);
            preferences_.set("edge_width",contrast ? 48 : 32);
            preferences_.set("hostile",contrast ? 0xb8ff675e : 0x9cf06b63);
            preferences_.set("friendly",contrast ? 0xb071baff : 0x944fa9f5);
            preferences_.set("neutral",contrast ? 0xb8ffd67a : 0x9ce6bd67);
            preferences_.set("safe",contrast ? 0xb05ce6b6 : 0x944ed6aa);
            report(contrast ? "High contrast encounter palette saved." : "Bastion encounter palette saved.");
        }
    } else if (*op != "state") return 0;
    result.begin(); result.boolean("active",selected_ && in_zone_); result.boolean("ready",session_.ready());
    result.number("markers",static_cast<std::int64_t>(session_.active_count()));
    result.number("local_count",static_cast<std::int64_t>(local_.size())); result.number("local_submitted",local_submitted_);
    result.string("message",message_); result.string("error",preferences_.error());
    result.key("settings"); result.begin();
    for (const auto& def : definitions) result.number(def.key,preferences_.get(def.key));
    result.end(); result.end(); return 1;
}
}
