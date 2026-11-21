#include "citadel_hub.h"
#include "citadel_services.h"
#include "raid_snapshot.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace bastion {
namespace {
constexpr Preference definitions[] = {
    {"right_click_sell",0,1,0},
    {"map_group",0,1,1},
    {"map_raid",0,1,1}, {"map_mobs",0,1,1}, {"map_merchants",0,1,1},
    {"map_players",0,1,1}, {"map_corpses",0,1,1}
};
constexpr std::array<std::string_view,6> map_keys{
    "map_group", "map_raid", "map_mobs", "map_merchants", "map_players", "map_corpses"
};
bool merchant(std::uint32_t c) { return c==41 || c==61 || c==67 || c==68 || c==70; }
}
std::span<const Preference> citadel_preferences() { return definitions; }
CitadelHub::CitadelHub() : map_raid_generation_(services::token()), map_revision_(services::token()) {}
void CitadelHub::retire_map_raid() const {
    map_raid_.owner = 0;
    map_raid_.count = 0;
    map_raid_generation_ = services::token();
}
bool CitadelHub::map_raid_member(const HailstoneSpawn& actor,const HailstonePlayer& player,const HailstoneSpawn& me) const {
    if (map_raid_running_) { retire_map_raid(); return false; }
    if (!selected_ || !in_game_ || !me.incarnation || !HAILSTONE_HOST_HAS(host_,get_spawn)) return false;
    if (map_raid_.owner != me.incarnation || map_raid_.player != player.spawn_id ||
        map_raid_.zone != player.zone_id || map_raid_.instance != player.zone_instance) {
        // Bulk data has no revision or incarnations. Resolve a display-only snapshot once after each pulse.
        const auto* host = host_;
        const auto generation = map_raid_generation_;
        map_raid_running_ = true;
        struct Finish {
            bool& running;
            ~Finish() { running = false; }
        } finish{map_raid_running_};
        map_raid_ = {};
        const auto snapshot = raid::read(host);
        if (generation != map_raid_generation_) return false;
        MapRaid next;
        next.owner = me.incarnation;
        next.player = player.spawn_id;
        next.zone = player.zone_id;
        next.instance = player.zone_instance;
        if (snapshot && snapshot->header.active) {
            for (const auto& member : snapshot->members) {
                if (!member.spawnID) continue;
                HailstoneSpawn spawn{};
                spawn.size = sizeof(spawn);
                const auto found = host->get_spawn(host->host, member.spawnID, &spawn);
                if (generation != map_raid_generation_) return false;
                if (!found || spawn.id != member.spawnID ||
                    spawn.size < offsetof(HailstoneSpawn, incarnation) + sizeof(spawn.incarnation) ||
                    !spawn.incarnation) {
                    next.count = 0;
                    break;
                }
                const auto name = services::bounded(member.name);
                if (spawn.is_npc || spawn.is_corpse || name.empty() ||
                    !raid::same_name(name, services::bounded(spawn.name))) continue;
                next.actors[next.count++] = {spawn.id, spawn.incarnation};
            }
            auto observations = std::span(next.actors).first(next.count);
            std::ranges::sort(observations);
            if (std::ranges::adjacent_find(observations, {}, &MapRaid::Actor::first) != observations.end())
                next.count = 0;
        }
        map_raid_ = next;
    }
    const auto observations = std::span(map_raid_.actors).first(map_raid_.count);
    return std::ranges::binary_search(observations, std::pair{actor.id, actor.incarnation});
}
void CitadelHub::observe_map_settings() {
    std::array<std::int64_t,6> values{};
    for (std::size_t index=0;index<map_keys.size();++index) values[index]=settings_.get(map_keys[index]);
    if (values==map_values_) return;
    map_values_=values;
    // Provider content revisions never wrap or depend on how often a window paints.
    if (const auto revision=services::token()) map_revision_=revision;
}
void CitadelHub::refresh_settings() { settings_.refresh(host_); observe_map_settings(); }
bool CitadelHub::set_preference(std::string_view key,std::int64_t value) {
    const auto definition=std::find_if(std::begin(definitions),std::end(definitions),[&](const auto& item) {
        return key==item.key;
    });
    if (definition!=std::end(definitions) && value>=definition->low && value<=definition->high && settings_.get(key)==value)
        return true;
    const bool saved=settings_.set(key,value);
    observe_map_settings();
    return saved;
}
void CitadelHub::set_host(const PluginHostApi* host) {
    if (HAILSTONE_HOST_HAS(host_,remove_ui_function)) host_->remove_ui_function(host_->host,"citadel");
    host_ = host; peer(false); open_ = {}; refresh_settings();
}
void CitadelHub::initialize(std::function<bool(std::string_view)> open) {
    open_ = std::move(open);
    if (HAILSTONE_HOST_HAS(host_,add_ui_function)) host_->add_ui_function(host_->host,"citadel",ui_callback,this);
    if (HAILSTONE_HOST_HAS(host_,add_menu)) host_->add_menu(host_->host,"Bastion","/bastion");
}
void CitadelHub::peer(bool selected) { retire_map_raid();settings_.invalidate_context(); selected_ = selected; shown_ = false; panel_ = "general"; message_.clear(); }
void CitadelHub::game_state(int state) { retire_map_raid();settings_.invalidate_context(); in_game_ = state == GAMESTATE_INGAME; if (!in_game_) shown_ = false; }
void CitadelHub::begin_zone() { retire_map_raid();settings_.invalidate_context(); in_game_ = false; shown_ = false; }
void CitadelHub::pulse() { retire_map_raid(); refresh_settings(); }
void CitadelHub::open(std::string_view panel) { shown_ = selected_ && in_game_; panel_ = std::string(panel); ++open_revision_; }
void CitadelHub::report(std::string_view text) {
    message_ = std::string(text);
    if (HAILSTONE_HOST_HAS(host_,write_chat)) host_->write_chat(host_->host,message_.c_str(),0);
}
std::uint32_t CitadelHub::timestamp_style() const { return 0; }
bool CitadelHub::right_click_sell() const { return selected_ && in_game_ && settings_.get("right_click_sell") != 0; }
float CitadelHub::tracking_range() const {
    if (!selected_ || !in_game_ || !HAILSTONE_HOST_HAS(host_,get_player) || !HAILSTONE_HOST_HAS(host_,get_spawn)) return 0;
    HailstonePlayer player{}; player.size = sizeof(player);
    HailstoneSpawn actor{}; actor.size = sizeof(actor);
    if (!host_->get_player(host_->host,&player) || !player.in_zone || !host_->get_spawn(host_->host,player.spawn_id,&actor)) return 0;
    if (actor.is_gm) return 1000000;
    // Citadel's map permissions use the actual tracking class, received skill
    // and its memorized tracking techniques. No class or skill is spoofed.
    if ((player.class_id != 4 && player.class_id != 6 && player.class_id != 8) || !HAILSTONE_HOST_HAS(host_,skill)) return 0;
    const auto skill = host_->skill(host_->host,53);
    if (skill <= 0 || skill >= 253) return 0;
    int technique = 10;
    constexpr int amounts[] = {50,100,150,200,250,300,350,450,500,750,1000};
    if (HAILSTONE_HOST_HAS(host_,get_memorized_spell)) for (std::uint32_t index=0;index<16;++index) {
        const auto spell = host_->get_memorized_spell(host_->host,index);
        if (spell>=7864 && spell<=7874) technique += amounts[spell-7864];
    }
    // Citadel's configured map range is one fifth of its tracking range.
    return std::floor(static_cast<float>(200 + skill*20 + technique) * .2f);
}
int CitadelHub::map_policy(HailstoneMapPolicy* out) const {
    constexpr auto legacy_size=offsetof(HailstoneMapPolicy,npc_find_mode);
    if (!selected_ || !out || out->size<legacy_size) return 0;
    const bool server_find=out->size>=sizeof(*out);
    HailstoneMapPolicy policy{};
    policy.size=static_cast<std::uint32_t>(server_find ? sizeof(policy) : legacy_size);
    policy.revision=map_revision_;
    policy.flags=HAILSTONE_MAP_POLICY_CUSTOM_SPAWNS|HAILSTONE_MAP_POLICY_HIDE_STOCK_NPCS;
    if (server_find) policy.npc_find_mode=HAILSTONE_NPC_FIND_SERVER_ONLY;
    else policy.flags|=HAILSTONE_MAP_POLICY_DISABLE_NPC_FIND;
    std::memcpy(policy.name,"Bastion",8);
    std::memcpy(out,&policy,policy.size);
    return 1;
}
int CitadelHub::map_visible(const HailstoneSpawn* actor,std::uint32_t purpose) const {
    if (!selected_) return -1;
    if (!in_game_ || !actor || actor->size<offsetof(HailstoneSpawn,incarnation)+sizeof(actor->incarnation) ||
        !actor->incarnation || !HAILSTONE_HOST_HAS(host_,get_player) || !HAILSTONE_HOST_HAS(host_,get_spawn)) return 0;
    HailstonePlayer player{}; player.size=sizeof(player);
    HailstoneSpawn me{}; me.size=sizeof(me);
    if (!host_->get_player(host_->host,&player) || !player.in_zone || !host_->get_spawn(host_->host,player.spawn_id,&me)) {
        retire_map_raid(); return 0;
    }
    // Hailstone checks the server's Find list before calling this hook for an NPC.
    if (purpose==HAILSTONE_MAP_FIND && (actor->is_npc || actor->is_corpse)) return !actor->is_corpse;
    if (actor->id==player.spawn_id) return 1;
    const bool pc=!actor->is_npc && !actor->is_corpse;
    if (pc && actor->in_group) {
        // This is a presentation choice, never a restriction on group Find/navigation.
        return purpose==HAILSTONE_MAP_MARKER ? (settings_.get("map_group") ? 1 : 0) : 1;
    }
    if (purpose==HAILSTONE_MAP_FIND && pc && me.is_gm) return 1;
    if (pc && settings_.get("map_raid")) {
        const auto generation=map_raid_generation_;
        const bool raid=map_raid_member(*actor,player,me);
        if (!selected_ || !in_game_ || generation!=map_raid_generation_) return 0;
        if (raid) return 1;
    }
    // Explicit category switches also apply to GMs. GM status bypasses range/eligibility only after the
    // selected category is enabled; corpses are classified before their former NPC/merchant class.
    const auto category=actor->is_corpse ? "map_corpses" : pc ? "map_players" :
        merchant(actor->class_id) ? "map_merchants" : "map_mobs";
    if (!settings_.get(category)) return 0;
    if (me.is_gm) return 1;
    const auto range=tracking_range();
    if (range<=0) return 0;
    if (pc) return 1;
    const auto distance=std::hypot(actor->x-me.x,actor->y-me.y);
    if (!std::isfinite(distance) || distance>range) return 0;
    return 1;
}
int CitadelHub::map_spawn(const HailstoneSpawn* actor,HailstoneMapSpawn* out) {
    if (!selected_ || !in_game_ || !actor || !out || actor->size < offsetof(HailstoneSpawn,incarnation) + sizeof(actor->incarnation) ||
        out->size < sizeof(*out) || !actor->incarnation || !HAILSTONE_HOST_HAS(host_,get_player)) return 0;
    HailstonePlayer player{}; player.size = sizeof(player);
    if (!host_->get_player(host_->host,&player) || !player.in_zone) { retire_map_raid(); return 0; }
    if (map_visible(actor,HAILSTONE_MAP_MARKER)!=1) return 0;
    HailstoneSpawn me{}; me.size=sizeof(me);
    if (!HAILSTONE_HOST_HAS(host_,get_spawn) || !host_->get_spawn(host_->host,player.spawn_id,&me)) return 0;
    const auto generation=map_raid_generation_;
    const bool raid=!actor->is_npc && !actor->is_corpse && settings_.get("map_raid") && map_raid_member(*actor,player,me);
    if (!selected_ || !in_game_ || generation!=map_raid_generation_) return 0;
    out->layer = 0;
    if (raid) out->color=0x80ecb0;
    if (!me.is_gm && !actor->is_npc && !actor->is_corpse && !actor->in_group && !raid) { out->label[0]=0; return 1; }
    const auto name = services::bounded(actor->display_name).empty() ? services::bounded(actor->name) : services::bounded(actor->display_name);
    const auto size = std::min(name.size(),sizeof(out->label)-1); std::memcpy(out->label,name.data(),size); out->label[size]=0;
    return 1;
}
int CitadelHub::merchant_right_click(const HailstoneMerchantSellContext* context) {
    if (!right_click_sell() || !context || context->size<sizeof(*context) || !context->merchant_id ||
        !context->item_id || !context->quantity || !HAILSTONE_HOST_HAS(host_,send_packet)) return 0;
    if(context->flags) {
        if(context->flags&HAILSTONE_MERCHANT_CURSOR_OCCUPIED) report("Right-click selling skipped: your cursor is holding an item.");
        else if(context->flags&HAILSTONE_MERCHANT_KEPT) report("Right-click selling skipped: this item is marked Keep.");
        else report("Right-click selling skipped: this item cannot be sold.");
        return 1;
    }
    // The opcode belongs to the addon. The host supplies a freshly validated
    // user click; it neither knows this wire contract nor performs the sale.
    hailstone::ByteWriter payload;
    payload.le(std::uint32_t{0x43535253}); payload.le(std::uint32_t{1});
    const auto& slot=context->item_slot;
    if(slot.type!=0 || slot.slot<0 || slot.slot>32 || slot.aug!=-1 || slot.sub< -1 || slot.sub>=200 ||
        (slot.sub>=0 && slot.slot<23)) return 0;
    const auto server_slot=slot.sub<0 ? static_cast<std::uint32_t>(slot.slot) :
        4010u+static_cast<std::uint32_t>(slot.slot-23)*200u+static_cast<std::uint32_t>(slot.sub);
    payload.le(server_slot); payload.le(context->quantity); payload.le(context->item_id);
    if (!host_->send_packet(host_->host,HAILSTONE_STREAM_ZONE,0x7d1b,payload.data().data(),payload.size()))
        report("Right-click sale could not be sent. Please try again.");
    // A failed sale still consumes the click, avoiding accidental item use.
    return 1;
}
bool CitadelHub::command(std::string_view arguments) {
    refresh_settings();
    std::istringstream stream{std::string(arguments)};
    std::string action,key,value,extra; stream>>action>>key>>value>>extra;
    if (action.empty() || action=="options" || action=="settings") { open(); return true; }
    if (action=="help") { report("Bastion: /bastion opens tools, settings and server commands. Includes follow, raidbars, faction, petinventory, dz, homecity, gear, telegraphs and firefell. Right-click selling defaults off."); return true; }
    if ((action=="rightclicksell" || action=="maps") && extra.empty()) {
        const auto name = action=="maps" ? "map_"+key : "right_click_sell";
        const auto on = action=="maps" ? value : key;
        if (action=="maps" && key!="raid" && key!="group" && tracking_range()<=0) { report("These map filters require a GM or a tracking class with a trained tracking skill."); return true; }
        const bool toggle=action=="rightclicksell" && (on.empty() || on=="toggle");
        if ((on=="on" || on=="off" || toggle) && set_preference(name,toggle ? !settings_.get(name) : on=="on"?1:0)) report("Citadel preference saved.");
        else report("Usage: /citadel maps group|raid|mobs|merchants|players|corpses on|off; /rightclicksell on|off");
        return true;
    }
    return false;
}
int CitadelHub::ui_callback(void* self,const HailstoneArgs* args,HailstoneOut* out) noexcept {
    if (!self) return 0;
    auto &owner = *static_cast<CitadelHub*>(self);
    return services::guarded_ui(owner.host_, [&] { return owner.ui(args,out); });
}
int CitadelHub::ui(const HailstoneArgs* args,HailstoneOut* out) {
    services::Output result{host_,out}; if (!result.valid()) return 0;
    const auto op = services::argument(host_,args,0,16); if (!op) return 0;
    if (*op=="visibility") {
        result.begin();result.boolean("active",selected_&&in_game_);result.boolean("shown",selected_&&in_game_&&shown_);
        result.identity("open_revision",open_revision_);result.string("panel",panel_);result.end();return 1;
    }
    refresh_settings();
    if (*op=="show") open();
    else if (*op=="close") shown_=false;
    else if (*op=="panel") { const auto panel=services::argument(host_,args,1,24); if (panel) open(*panel); }
    else if (*op=="open") { const auto feature=services::argument(host_,args,1,24); if (feature && open_) open_(*feature); }
    else if (*op=="set") {
        const auto key=services::argument(host_,args,1,24); HailstoneValue value{};
        if (key && host_->arg(host_->host,args,2,&value) && value.kind==HAILSTONE_INT) {
            if (key->starts_with("map_") && (!selected_ || !in_game_)) message_="Map filters are unavailable outside the current Bastion zone.";
            else if (key->starts_with("map_") && *key!="map_raid" && *key!="map_group" && tracking_range()<=0) message_="This map filter requires tracking or GM status.";
            else set_preference(*key,value.i);
        }
    } else if (*op!="state") return 0;
    result.begin(); result.boolean("active",selected_&&in_game_); result.boolean("shown",shown_);
    result.identity("open_revision",open_revision_); result.string("panel",panel_); result.string("message",message_);
    result.identity("map_revision",map_revision_);
    result.string("error",settings_.error()); result.boolean("can_track",tracking_range()>0);
    result.key("settings"); result.begin(); for (const auto& def:definitions) result.number(def.key,settings_.get(def.key)); result.end();
    result.end(); return 1;
}
}
