// SPDX-License-Identifier: MIT
#include "hailstone_plugin.h"
#include "hailstone_byte_io.h"
#include "citadel_hub.h"
#include "follow_addon.h"
#include "raidbars.h"
#include "raid_ready_check.h"
#include "expedition_journal.h"
#include "tradeskill_plans.h"
#include "factions_addon.h"
#include "saved_expeditions_addon.h"
#include "pet_inventory_addon.h"
#include "home_city_addon.h"
#include "gear_score_addon.h"
#include "kinbound.h"
#include "telegraphs_adapter.h"
#include "firefell.h"
#include "server_commands.h"
#include "integrity.h"
#include "twist.h"
#include "fast_camp.h"
#include <algorithm>
#include <cstring>
#include <exception>
#include <string>
#include <string_view>

HAILSTONE_PLUGIN

namespace {
static_assert(bastion::ready_check::kOpcode != bastion::gear_score::kOpcode);
static_assert(bastion::ready_check::kOpcode != bastion::firefell::opcode);
static_assert(bastion::journal::kOpcode != bastion::ready_check::kOpcode);
static_assert(bastion::tradeskill_plans::kOpcode != bastion::journal::kOpcode);

bool selected=false, initialized=false, saw_world_announcement=false;
bastion::CitadelHub hub;
bastion::follow::Addon follow;
bastion::raidbars::Adapter raid;
bastion::ready_check::Adapter ready_check;
bastion::journal::Adapter journal;
bastion::tradeskill_plans::Adapter tradeskill_plans;
bastion::factions::Adapter factions;
bastion::saved_expeditions::Adapter expeditions;
bastion::pet_inventory::Adapter pets;
bastion::home_city::Adapter home;
bastion::gear_score::Addon gear;
bastion::KinboundCatalog kinbound;
bastion::telegraphs::Adapter telegraphs;
bastion::firefell::Adapter firefell;
bastion::ServerCommands commands;
bastion::integrity::Adapter integrity;
bastion::twist::Adapter twist;
bastion::fast_camp::Adapter fast_camp;
std::string description;
std::string corpse_command;

template<class F> void guarded(F&& action) noexcept {
    bastion::services::guarded_ui(host,[&] { action(); return 1; });
}
void peer(bool value) {
    bool map_support=false;
    if(value && HAILSTONE_HOST_HAS(host,get_map_policy)) {
        try {
            HailstoneMapPolicy policy{};policy.size=sizeof(policy);
            constexpr auto required=offsetof(HailstoneMapPolicy,context)+sizeof(policy.context);
            // Older readers preserve the caller's size while copying only their prefix.
            // A world announcement has a host-issued context in the extended record.
            map_support=host->get_map_policy(host->host,&policy)!=0 && policy.size>=required && policy.context!=0;
        } catch(...) {}
    }
    if(value && (!HAILSTONE_HOST_HAS(host,world_surface_patch) || !HAILSTONE_HOST_HAS(host,measure_binary) ||
                 !map_support)) {
        value=false;
        if(HAILSTONE_HOST_HAS(host,write_chat)) host->write_chat(host->host,"Bastion needs the matching Hailstone update for server addons and maps.",0);
        if(HAILSTONE_HOST_HAS(host,log)) host->log(host->host,2,"Bastion addon requires server addon services and map policy revision/context support; features remain inactive.");
    }
    selected=value;
    hub.peer(value); follow.peer(value); raid.peer(value); ready_check.peer(value); journal.peer(value); twist.peer(value); fast_camp.peer(value);
    tradeskill_plans.peer(value);
    factions.peer(value); expeditions.peer(value); pets.peer(value);
    home.peer(value); gear.peer(value); telegraphs.peer(value); firefell.peer(value); commands.peer(value); integrity.peer(value); kinbound.clear();
}
std::string lower(std::string_view text) {
    std::string result(text);
    for (auto& c:result) if(c>='A' && c<='Z') c=static_cast<char>(c+'a'-'A');
    return result;
}
bool official_world() {
    if (!HAILSTONE_HOST_HAS(host, server_info)) return false;
    HailstoneServer server{};
    server.size = sizeof(server);
    return host->server_info(host->host, &server) &&
           lower(bastion::services::bounded(server.host)) == "login.bastiongame.com" &&
           lower(bastion::services::bounded(server.world)) == "bastion - production";
}
void state(int value) {
    if (value==GAMESTATE_PRECHARSELECT || value==GAMESTATE_UNLOADING) {
        saw_world_announcement=false;
        peer(false);
    }
    // With no cached rules or a late consent, required plugins can first load at zone-in.
    if (value==GAMESTATE_INGAME && !selected && !saw_world_announcement && official_world()) peer(true);
    hub.game_state(value); follow.game_state(value); raid.game_state(value); ready_check.game_state(value); journal.game_state(value);
    tradeskill_plans.game_state(value);
    factions.game_state(value); expeditions.game_state(value); pets.game_state(value);
    home.game_state(value); gear.game_state(value); telegraphs.game_state(value); firefell.game_state(value);
    integrity.game_state(value); twist.game_state(value); fast_camp.game_state(value);
    if(value==GAMESTATE_CHARSELECT || value==GAMESTATE_PRECHARSELECT || value==GAMESTATE_UNLOADING) kinbound.clear();
}
std::string_view word(std::string_view& text) {
    while(!text.empty() && (text.front()==' ' || text.front()=='\t')) text.remove_prefix(1);
    const auto end=text.find_first_of(" \t"); const auto result=text.substr(0,end);
    text=end==text.npos ? std::string_view{} : text.substr(end+1);
    while(!text.empty() && (text.front()==' ' || text.front()=='\t')) text.remove_prefix(1);
    return result;
}
bool family(std::string_view raw) {
    const auto name=lower(raw);
    if(name=="bastion" || name=="bastiondev" || name=="basdev") return true;
    if(!name.starts_with("bastion") || name.size()<=7) return false;
    if(name[7]!='_' && name[7]!='-' && !(name[7]>='0' && name[7]<='9')) return false;
    if((name[7]=='_' || name[7]=='-') && name.size()==8) return false;
    return std::all_of(name.begin()+7,name.end(),[](char c) {
        return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-';
    });
}
bool open(std::string_view feature) {
    if(feature=="faction" || feature=="factions") return factions.command("open");
    if(feature=="dz" || feature=="dzmanager") return expeditions.command("open");
    if(feature=="expeditionjournal" || feature=="journal") return journal.open();
    if(feature=="tradeskillplans") return tradeskill_plans.open();
    if(feature=="petinventory") return pets.command("open");
    if(feature=="homecity") return home.command("open");
    if(feature=="raidbars" || feature=="citadel_follow" || feature=="telegraphs" || feature=="gear_score" || feature=="firefell" ||
        feature=="settings" || feature=="commands" || feature=="tools") { hub.open(feature); return true; }
    return false;
}
const char* command_impl(const char* line) {
    if(!selected || !line) return nullptr;
    constexpr std::size_t max_line=1024;
    std::size_t length=0; while(length<=max_line && line[length]) ++length;
    if(length>max_line || std::string_view(line,length).find_first_of("\r\n")!=std::string_view::npos) return nullptr;
    std::string_view args(line,length);
    const auto root=lower(word(args));
    if(root=="/hcl") return "/hidecorpses looted";
    if(root=="/hca") return "/hidecorpses all";
    if(root=="/hcn") return "/hidecorpses none";
    if(root=="/hc" || root=="/hideco" || root=="/hidec") {
        constexpr std::string_view native="/hidecorpses";
        if(args.size()>max_line-native.size()-1) return nullptr;
        corpse_command=native;
        if(!args.empty()) { corpse_command+=' '; corpse_command+=args; }
        return corpse_command.c_str();
    }
    twist.observe_command(line);
    fast_camp.observe_command(line);
    auto panel_args=args;
    if(root=="/bastion" && lower(word(panel_args))=="follow" && panel_args.empty()) {
        hub.open("citadel_follow"); return "";
    }
    const auto normalized=root=="/cit" || root=="/bastion" ? "/citadel "+std::string(args) : std::string(line);
    if(follow.command(normalized)) return "";
    if(twist.command(line)) return "";
    if(fast_camp.command(line)) return "";
    if(root=="/faction" || root=="/factions") { factions.command(args); return ""; }
    if(root=="/petinventory" || root=="/petgear") { pets.command(args); return ""; }
    if(root=="/dzmanager") { expeditions.command(args); return ""; }
    if(root=="/expeditionjournal") { if(args.empty()) journal.open(); return ""; }
    if(root=="/tradeskillplans") { if(args.empty()) tradeskill_plans.open(); return ""; }
    if(root=="/raidbars") { if(args.empty()) hub.open("raidbars"); else raid.command(args); return ""; }
    if(root=="/rightclicksell") { hub.command("rightclicksell "+lower(args)); return ""; }
    if(root=="/setstartcity") { home.command(args); return ""; }
    if(root=="/telegraphs" || root=="/telegraph") { if(args.empty()) hub.open("telegraphs"); else telegraphs.command(lower(args)); return ""; }
    if(root=="/gearratings") { hub.open("gear_score"); return ""; }
    if(root=="/firefell") { hub.open("firefell"); return ""; }
    if(root=="/outputfile") { const auto kind=lower(word(args)); if(kind=="faction" || kind=="factions") { factions.export_file(args); return ""; } return nullptr; }
    if(commands.shortcut(line)) return "";
    if(root!="/citadel" && root!="/cit" && root!="/bastion") return nullptr;
    const auto original=args;
    const auto feature=lower(word(args));
    if(feature=="twist" || feature=="sing" || feature=="stoptwist") twist.command("/"+feature+" "+std::string(args));
    else if(feature=="fastcamp") fast_camp.command("/fastcamp "+std::string(args));
    else if(feature=="faction" || feature=="factions") factions.command(args);
    else if(feature=="dz" || feature=="dzmanager") expeditions.command(args);
    else if((feature=="expeditionjournal" || feature=="journal") && args.empty()) journal.open();
    else if(feature=="tradeskillplans" && args.empty()) tradeskill_plans.open();
    else if(feature=="petinventory" || feature=="petgear") pets.command(args);
    else if(feature=="raidbars") { if(args.empty()) hub.open("raidbars"); else raid.command(args); }
    else if(feature=="telegraphs") { if(args.empty()) hub.open("telegraphs"); else telegraphs.command(lower(args)); }
    else if(feature=="homecity") home.command(args);
    else if(feature=="gear" || feature=="ratings") hub.open("gear_score");
    else if(feature=="firefell") hub.open("firefell");
    else if(feature=="commands" || feature=="settings" || feature=="tools") hub.open(feature);
    else if(feature=="follow" && args.empty()) hub.open("citadel_follow");
    else if(!hub.command(lower(original)) && HAILSTONE_HOST_HAS(host,write_chat)) host->write_chat(host->host,"Use /citadel help.",0);
    return "";
}
const char* command(const char* line) noexcept {
    const char* result=nullptr; guarded([&] { result=command_impl(line); }); return result;
}
int packet(int stream,int direction,HailstonePacket* p) {
    if(!initialized || !p) return 0;
    if(stream==HAILSTONE_STREAM_WORLD) {
        if(direction==HAILSTONE_OUT && p->opcode==0x7a09) {
            // RoF2 LoginInfo's zoning flag is the sole field needed here. Never
            // inspect or retain its account/key strings. Invalid records retire state.
            const bool zoning=p->data&&p->size==464&&p->data[188]==1;
            integrity.world_login(zoning);
            twist.world_login(zoning);
            saw_world_announcement=false;
            peer(false);
        }
        if(direction==HAILSTONE_IN && p->opcode==0x7ceb) {
            saw_world_announcement=true;
            peer(false);
            if(p->data && p->size==560) {
                const char* name=reinterpret_cast<const char*>(p->data)+36;
                const char* end=std::find(name,name+32,'\0');
                if(end!=name+32) peer(family(std::string_view(name,static_cast<std::size_t>(end-name))));
            }
        }
        if(selected && direction==HAILSTONE_IN && integrity.packet(p->opcode,p->data,p->size)) return 1;
        if(direction==HAILSTONE_OUT && bastion::integrity::login_checkpoint(p->opcode)) {
            // Late consent may load the addon after LogServer but before EnterWorld.
            // Only the exact official endpoint can fill in a missed announcement.
            if(!selected && !saw_world_announcement && official_world()) peer(true);
            if(selected) integrity.world_outgoing(p->opcode);
        }
    }
    if(selected && stream==HAILSTONE_STREAM_ZONE && direction==HAILSTONE_OUT) fast_camp.outgoing(p->opcode);
    if(!selected || stream!=HAILSTONE_STREAM_ZONE || direction!=HAILSTONE_IN) return 0;
    if(p->opcode==0x7d20) { kinbound.receive(p->data,p->size); return 1; }
    if(p->opcode==0x7d1f) { telegraphs.receive(p->data,p->size); return 1; }
    return factions.packet(p->opcode,p->data,p->size) || expeditions.packet(p->opcode,p->data,p->size) ||
        pets.packet(p->opcode,p->data,p->size) || home.packet(p->opcode,p->data,p->size) ||
        gear.packet(p->opcode,p->data,p->size) || firefell.packet(p->opcode,p->data,p->size) ||
        ready_check.packet(p->opcode,p->data,p->size) || journal.packet(p->opcode,p->data,p->size) ||
        tradeskill_plans.packet(p->opcode,p->data,p->size);
}
}
HAILSTONE_API uint32_t hailstone_plugin_caps(void) {
    return HAILSTONE_CAP_READ|HAILSTONE_CAP_COMMANDS|HAILSTONE_CAP_TARGET|HAILSTONE_CAP_ACTIONS|
        HAILSTONE_CAP_MOVE|HAILSTONE_CAP_CHAT|HAILSTONE_CAP_EVENTS|HAILSTONE_CAP_UI|
        HAILSTONE_CAP_PACKETS|HAILSTONE_CAP_EXTEND|HAILSTONE_CAP_LOGIN;
}
HAILSTONE_API void InitializePlugin(void) noexcept { guarded([] {
    hub.set_host(host); follow.set_host(host); raid.set_host(host); ready_check.set_host(host); journal.set_host(host);
    tradeskill_plans.set_host(host);
    factions.set_host(host); expeditions.set_host(host); pets.set_host(host);
    home.set_host(host); gear.set_host(host); telegraphs.set_host(host); firefell.set_host(host); commands.set_host(host); integrity.set_host(host); twist.set_host(host); fast_camp.set_host(host);
    hub.initialize(open); follow.initialize(); raid.initialize(); ready_check.initialize(); journal.initialize(); factions.initialize(); expeditions.initialize(); pets.initialize();
    tradeskill_plans.initialize();
    home.initialize(); gear.initialize(); telegraphs.initialize(); firefell.initialize(); commands.initialize(); integrity.initialize(); twist.initialize(); fast_camp.initialize();
    saw_world_announcement=false;
    peer(false); initialized=true;
    HailstonePlayer player{};
    player.size = sizeof(player);
    if (HAILSTONE_HOST_HAS(host, get_player) && host->get_player(host->host, &player) && player.in_zone)
        state(player.game_state);
}); }
HAILSTONE_API void ShutdownPlugin(void) noexcept { guarded([] {
    initialized=false; peer(false);
    hub.set_host(nullptr); follow.set_host(nullptr); raid.set_host(nullptr); ready_check.set_host(nullptr); journal.set_host(nullptr);
    tradeskill_plans.set_host(nullptr);
    factions.set_host(nullptr); expeditions.set_host(nullptr); pets.set_host(nullptr);
    home.set_host(nullptr); gear.set_host(nullptr); telegraphs.set_host(nullptr); firefell.set_host(nullptr); commands.set_host(nullptr); integrity.set_host(nullptr); twist.set_host(nullptr); fast_camp.set_host(nullptr);
}); }
HAILSTONE_API void SetGameState(int value) noexcept { guarded([&] { state(value); }); }
HAILSTONE_API void OnZoned(void) noexcept { guarded([] { state(GAMESTATE_INGAME); }); }
HAILSTONE_API void OnBeginZone(void) noexcept { guarded([] {
    hub.begin_zone(); follow.begin_zone(); raid.begin_zone(); ready_check.begin_zone(); journal.begin_zone(); factions.begin_zone(); expeditions.begin_zone(); pets.begin_zone();
    tradeskill_plans.begin_zone();
    home.begin_zone(); gear.begin_zone(); telegraphs.begin_zone(); firefell.begin_zone(); twist.begin_zone(); fast_camp.begin_zone();
}); }
HAILSTONE_API void OnPulse(void) noexcept { guarded([] {
    hub.pulse(); follow.pulse(); raid.pulse(); ready_check.pulse(); journal.pulse(); factions.pulse(); expeditions.pulse(); pets.pulse();
    tradeskill_plans.pulse();
    home.pulse(); gear.pulse(); telegraphs.pulse(); firefell.pulse(); integrity.pulse(); twist.pulse(); fast_camp.pulse();
}); }
HAILSTONE_API int OnIncomingChat(const char* line,std::uint32_t) noexcept {
    if(initialized && selected && line) guarded([&] { twist.incoming_chat(line); });
    return 0;
}
HAILSTONE_API int OnPacket(int stream,int direction,HailstonePacket* p) noexcept {
    int result=0; guarded([&] { result=packet(stream,direction,p); }); return result;
}
HAILSTONE_API const HailstoneHooks* hailstone_plugin_hooks(void) {
    static const auto hooks=[] {
        HailstoneHooks h{}; h.size=sizeof(h); h.command_line=command;
        h.follow_profile=[](HailstoneFollowProfile* out) { int r=0; guarded([&] { r=follow.follow_profile(out); }); return r; };
        h.mount_profile=[](HailstoneMountProfile* out) { int r=0; guarded([&] { r=follow.mount_profile(out); }); return r; };
        h.map_spawn=[](const HailstoneSpawn* s,HailstoneMapSpawn* out) { int r=0; guarded([&] { r=hub.map_spawn(s,out); }); return r; };
        h.map_visible=[](const HailstoneSpawn* s,uint32_t purpose) { int r=selected ? 0 : -1; guarded([&] { r=hub.map_visible(s,purpose); }); return r; };
        h.map_policy=[](HailstoneMapPolicy* out) { int r=0; guarded([&] { r=hub.map_policy(out); }); return r; };
        h.merchant_right_click=[](const HailstoneMerchantSellContext* c) { int r=0; guarded([&] { r=hub.merchant_right_click(c); }); return r; };
        h.zone_display_name=[](uint32_t zone,uint32_t instance)->const char* { const char* r=nullptr; guarded([&] { r=firefell.name(zone,instance); }); return r; };
        h.item_flags=[](const HailstoneItem* item,const char* flags)->const char* { const char* r=nullptr; if(selected) guarded([&] { r=kinbound.item_flags(item,flags); }); return r; };
        h.item_description_append=[](const HailstoneItem* item)->const char* {
            if(!selected) return nullptr;
            const char* r=nullptr; guarded([&] { const auto* k=kinbound.item_description_append(item); description=k ? k : ""; if(const auto* g=gear.item_description_append(item)) { if(!description.empty()) description+="\n\n"; description+=g; } r=description.empty() ? nullptr : description.c_str(); }); return r;
        };
        h.item_presentation_revision=[] { return selected ? kinbound.revision()+gear.revision() : std::uint64_t{0}; };
        h.loot_confirmation=[](const HailstoneItem* item,const uint32_t* ids,std::size_t count,int complete)->const char* { const char* r=nullptr; if(selected) guarded([&] { r=kinbound.loot_confirmation(item,ids,count,complete!=0); }); return r; };
        h.label=[](int id,const char*)->const char* { const char* r=nullptr; if(selected) guarded([&] { r=gear.label(id); }); return r; };
        h.world_frame=[](const HailstoneWorldFrame* frame) { guarded([&] { telegraphs.world_frame(frame); }); };
        h.world_frame_result=[](const HailstoneWorldReceipt* receipt) { guarded([&] { telegraphs.world_result(receipt); }); };
        return h;
    }();
    return &hooks;
}
