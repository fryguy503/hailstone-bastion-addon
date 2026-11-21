#include "citadel_hub.h"
#include "citadel_services.h"
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>
struct HailstoneArgs { std::vector<HailstoneValue> values; };
struct HailstoneOut {
    std::string key;
    std::map<std::string,std::int64_t> numbers;
    std::map<std::string,std::string> strings;
};
namespace {
unsigned checks=0;
void check(bool v,const char* why) { ++checks; if(!v) throw std::runtime_error(why); }
struct Rig {
    PluginHostApi api{};
    bastion::CitadelHub hub;
    HailstonePlayer player{};
    HailstoneSpawn me{};
    std::uint32_t technique=0;
    std::int32_t tracking_skill=50;
    bool player_available=true, spawn_available=true;
    struct RaidMember {
        std::uint32_t spawn_id;
        std::uint64_t incarnation;
        std::string_view roster_name = "FixtureRaider";
        std::string_view spawn_name = "fixturerAider";
    };
    std::vector<RaidMember> raid;
    unsigned roster_reads=0, player_reads=0, directory_reads=0;
    bool roster_available=true, partial_roster=false;
    std::uint32_t failed_member=0, truncated_member=0;
    std::function<void(Rig&)> on_roster;
    std::function<void(Rig&)> on_member;
    std::vector<std::byte> packet;
    std::uint16_t opcode=0;
    HailstoneUiFn ui=nullptr;
    void* ui_owner=nullptr;
    std::filesystem::path directory=std::filesystem::temp_directory_path()/("bastion-hub-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(bastion::services::token()));
    Rig() {
        api.size=sizeof(api); api.host=this;
        player.size=sizeof(player); player.spawn_id=1; player.in_zone=1; player.class_id=4;
        me.size=sizeof(me); me.id=1; me.incarnation=1;
        api.character_config_dir=[](void* h,char* out,std::size_t n) { auto& r=*static_cast<Rig*>(h);++r.directory_reads;auto s=r.directory.generic_string(); if(n<=s.size()) return 0; std::memcpy(out,s.c_str(),s.size()+1); return static_cast<int>(s.size()); };
        api.get_player=[](void* h,HailstonePlayer* p) { auto& r=*static_cast<Rig*>(h);++r.player_reads;if(!r.player_available)return 0; *p=r.player; return 1; };
        api.get_spawn=[](void* h,std::uint32_t id,HailstoneSpawn* s) {
            auto& r=*static_cast<Rig*>(h);
            if (id==r.me.id) {
                if (!r.spawn_available) return 0;
                *s=r.me;
                return 1;
            }
            if (const auto callback=r.on_member) callback(r);
            const auto member=std::ranges::find(r.raid,id,&RaidMember::spawn_id);
            if (member==r.raid.end() || id==r.failed_member) return 0;
            *s={};
            s->size=id==r.truncated_member ? offsetof(HailstoneSpawn,incarnation) : sizeof(*s);
            s->id=id;
            s->incarnation=member->incarnation;
            std::copy_n(member->spawn_name.data(),std::min(member->spawn_name.size(),sizeof(s->name)-1),s->name);
            return 1;
        };
        api.skill=[](void* h,std::uint32_t id) { return id==53 ? static_cast<Rig*>(h)->tracking_skill : 0; };
        api.get_raid_data_bulk=[](void* h,unsigned char* out,std::int32_t capacity) {
            auto& r=*static_cast<Rig*>(h);++r.roster_reads;
            if (const auto callback=r.on_roster) callback(r);
            if (!r.roster_available) return 0;
            HS_BulkRaidHeader header{};
            header.size=sizeof(header);
            header.version=1;
            header.recordSize=sizeof(HS_BulkRaidData);
            header.total=static_cast<std::uint32_t>(r.raid.size());
            header.count=header.total-(r.partial_roster && header.total ? 1u : 0u);
            header.active=header.total>0;
            const auto length=static_cast<std::int32_t>(header.size+header.count*header.recordSize);
            if (length>capacity) return 0;
            std::memcpy(out,&header,sizeof(header));
            for (std::uint32_t index=0;index<header.count;++index) {
                HS_BulkRaidData record{};
                record.spawnID=r.raid[index].spawn_id;
                const auto name=r.raid[index].roster_name;
                std::copy_n(name.data(),std::min(name.size(),sizeof(record.name)-1),record.name);
                std::memcpy(out+header.size+index*header.recordSize,&record,sizeof(record));
            }
            return length;
        };
        api.get_memorized_spell=[](void* h,std::uint32_t index) { return index==0 ? static_cast<Rig*>(h)->technique : 0u; };
        api.add_ui_function=[](void* h,const char*,HailstoneUiFn fn,void* owner) {
            auto& r=*static_cast<Rig*>(h);r.ui=fn;r.ui_owner=owner;return 1;
        };
        api.remove_ui_function=[](void* h,const char*) { auto& r=*static_cast<Rig*>(h);r.ui=nullptr;r.ui_owner=nullptr; };
        api.arg=[](void*,const HailstoneArgs* args,std::size_t index,HailstoneValue* out) {
            if(index>=args->values.size()) return 0;
            *out=args->values[index];
            return 1;
        };
        api.out_begin_table=[](void*,HailstoneOut*) {};
        api.out_begin_array=[](void*,HailstoneOut*) {};
        api.out_end=[](void*,HailstoneOut*) {};
        api.out_key=[](void*,HailstoneOut* out,const char* key) { out->key=key; };
        api.out_value=[](void*,HailstoneOut* out,const HailstoneValue* value) {
            if(value->kind==HAILSTONE_STRING)out->strings[out->key]=value->s ? value->s : "";
            else if(value->kind==HAILSTONE_INT||value->kind==HAILSTONE_BOOL)out->numbers[out->key]=value->i;
        };
        api.send_packet=[](void* h,int stream,std::uint16_t op,const void* data,std::size_t n) { auto& r=*static_cast<Rig*>(h); check(stream==HAILSTONE_STREAM_ZONE,"wrong merchant stream"); r.opcode=op; r.packet.assign(static_cast<const std::byte*>(data),static_cast<const std::byte*>(data)+n); return 1; };
        hub.set_host(&api); hub.initialize({}); hub.peer(true); hub.game_state(GAMESTATE_INGAME); hub.pulse();
    }
    ~Rig() { hub.set_host(nullptr); std::error_code ec; std::filesystem::remove_all(directory,ec); }
    HailstoneOut call(const char* operation,const char* key="",std::int64_t value=0) {
        HailstoneValue op{},name{},number{};op.kind=name.kind=HAILSTONE_STRING;op.s=operation;name.s=key;
        number.kind=HAILSTONE_INT;number.i=value;
        HailstoneArgs args{{op,name,number}};HailstoneOut out;
        check(ui&&ui(ui_owner,&args,&out)==1,"Citadel UI request failed");return out;
    }
};
std::uint64_t map_revision(const bastion::CitadelHub& hub) {
    HailstoneMapPolicy policy{};policy.size=sizeof(policy);
    check(hub.map_policy(&policy)==1&&policy.revision!=0,"map content revision is unavailable");return policy.revision;
}
void maps() {
    Rig r;
    HailstoneSpawn npc{}; npc.size=sizeof(npc); npc.id=2; npc.incarnation=2; npc.is_npc=1; npc.class_id=67;
    std::memcpy(npc.name,"Fixture",8); npc.x=242; npc.z=9999;
    HailstoneMapSpawn m{}; m.size=sizeof(m); m.color=0x123456;
    check(r.hub.map_spawn(&npc,&m)==1,"planar tracking boundary or merchant filter broken");
    check(m.color==0x123456,"map lost con color");
    npc.x=242.01f;
    check(r.hub.map_visible(&npc,HAILSTONE_MAP_MARKER)==0,"merchant marker exceeded Bastion tracking range");
    check(r.hub.map_visible(&npc,HAILSTONE_MAP_FIND)==1,"Tracking range denied server Find");
    r.technique=7864; npc.x=252; check(r.hub.map_spawn(&npc,&m)==1,"memorized technique absent");
    npc.x=1240; check(r.hub.map_spawn(&npc,&m)==0,"missing one-fifth multiplier");
    r.player.class_id=2; npc.x=1; check(r.hub.map_visible(&npc,HAILSTONE_MAP_MARKER)==0,"nontracker NPC reveal");
    npc.in_group=1; npc.is_npc=0; check(r.hub.map_visible(&npc,HAILSTONE_MAP_MARKER)==1,"group visibility lost");
    npc.in_group=0; r.player.class_id=4; npc.x=100000; check(r.hub.map_spawn(&npc,&m)==1 && m.label[0]==0,"unrelated PC parity broken");
    r.hub.peer(false); check(r.hub.map_visible(&npc,HAILSTONE_MAP_MARKER)==-1,"inactive map veto");
}
HailstoneSpawn map_actor() {
    HailstoneSpawn actor{};actor.size=sizeof(actor);actor.id=20;actor.incarnation=200;
    actor.is_npc=1;actor.class_id=1;std::memcpy(actor.name,"MapFixture",11);return actor;
}
void map_tracking_policy() {
    Rig r;auto actor=map_actor();actor.x=242;actor.z=100000;
    for(const auto class_id:{4u,6u,8u}) {
        r.player.class_id=class_id;
        check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"trained tracking class lost its planar boundary marker");
        actor.x=242.01f;
        check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"marker exceeded Bastion tracking range");
        actor.x=242;
    }
    r.tracking_skill=0;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"untrained tracker revealed an NPC");
    r.tracking_skill=253;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"unavailable tracking skill was interpreted as trained");
    r.tracking_skill=252;actor.x=1050;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"valid upper tracking skill lost its exact range boundary");
    actor.x=1050.01f;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"upper tracking skill exceeded its bounded range");
    r.tracking_skill=50;r.technique=7874;actor.x=442;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"received tracking technique did not extend the own-marker range");
    actor.x=442.01f;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"tracking technique range lost its one-fifth limit");
    actor.x=std::numeric_limits<float>::quiet_NaN();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"nonfinite NPC coordinates revealed a marker");
    actor.x=0;actor.y=std::numeric_limits<float>::infinity();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"infinite NPC coordinates revealed a marker");
    actor.y=0;r.me.x=std::numeric_limits<float>::quiet_NaN();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"nonfinite player coordinates admitted tracking");
}
void map_filters_and_social_visibility() {
    Rig r;auto actor=map_actor();actor.x=1;
    check(r.hub.command("maps merchants off"),"merchant filter command unavailable");
    for(const auto class_id:{41u,61u,67u,68u,70u}) {
        actor.class_id=class_id;
        check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"a merchant class bypassed its own disabled filter");
    }
    actor.class_id=1;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"merchant filter hid an ordinary NPC");
    check(r.hub.command("maps merchants on")&&r.hub.command("maps mobs off"),"independent NPC filters unavailable");
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"disabled NPC filter fell back to stock visibility");
    actor.class_id=67;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"ordinary NPC filter overrode the enabled merchant filter");
    actor.is_corpse=1;
    check(r.hub.command("maps corpses off")&&r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,
          "corpse filter did not override merchant classification");
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_FIND)==0,"corpse entered the stock NPC Find path");

    // Stored preferences stay enabled for future tracking classes; eligibility must remain authoritative now.
    check(r.hub.command("maps mobs on")&&r.hub.command("maps corpses on"),"default map preferences could not be restored");
    r.player.class_id=2;
    actor.is_corpse=0;actor.class_id=1;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"nontracker revealed an NPC with saved filters on");
    actor.class_id=67;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"nontracker revealed a merchant with saved filters on");
    actor.is_corpse=1;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"nontracker revealed a corpse with saved filters on");
    actor.is_npc=0;actor.is_corpse=0;actor.x=100000;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"nontracker revealed an unrelated player");
    actor.id=r.player.spawn_id;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"self visibility depended on Tracking");
    actor.id=20;actor.in_group=1;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"ordinary group visibility depended on Tracking");
    actor.in_group=0;
    r.raid.push_back({actor.id,actor.incarnation});
    r.hub.pulse();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"current raid member visibility depended on Tracking");
    HailstoneMapSpawn mark{};mark.size=sizeof(mark);
    check(r.hub.map_spawn(&actor,&mark)==1&&std::string(mark.label)=="MapFixture","raid marker lost its current member label");
    ++actor.incarnation;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"stale raid actor identity admitted another player");
    --actor.incarnation;
    check(r.hub.command("maps raid off")&&r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,
          "nontracker raid-name preference fell back to other-player visibility");
    r.player.class_id=4;
    check(r.hub.map_spawn(&actor,&mark)==1&&mark.label[0]==0,"tracker's unrelated-player dot parity was lost");
    check(r.hub.command("maps players off")&&r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,
          "disabled player markers fell back to stock visibility");
    actor.in_group=1;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"player marker filter hid an ordinary group member");
}
HailstoneSpawn raid_fixture(Rig& r) {
    r.player.class_id=2;
    for (std::uint32_t index=0;index<72;++index) r.raid.push_back({100+index,1000+index});
    auto actor=map_actor();actor.is_npc=0;actor.id=100;actor.incarnation=1000;
    return actor;
}
void map_raid_cache_scaling() {
    Rig r;auto actor=raid_fixture(r);
    HailstoneMapSpawn mark{};mark.size=sizeof(mark);
    for (std::uint32_t index=0;index<400;++index) {
        actor.id=100+index;actor.incarnation=1000+index;
        check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==(index<72?1:0),"cached raid membership changed marker admission");
        check(r.hub.map_spawn(&actor,&mark)==(index<72?1:0),"cached raid membership changed marker painting");
    }
    check(r.roster_reads==1,"unchanged markers fetched the bulk roster more than once per pulse");
    actor.id=100;actor.incarnation=1000;
    r.raid[0].incarnation=5000;r.hub.pulse();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0&&r.roster_reads==2,"changed roster retained an old actor incarnation");
    actor.incarnation=5000;
    check(r.hub.map_spawn(&actor,&mark)==1&&r.roster_reads==2,"new actor incarnation did not reuse the refreshed display snapshot");
    ++actor.incarnation;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0&&r.roster_reads==2,"same-ID replacement matched cached raid membership");
    actor.incarnation=5000;r.raid[0].spawn_name="OtherPlayer";r.hub.pulse();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"reused spawn ID matched a different raid name");
    actor.id=101;actor.incarnation=1001;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"name mismatch hid an unrelated valid raid member");
}
void map_raid_cache_lifetime() {
    Rig r;auto actor=raid_fixture(r);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"raid fixture unavailable");
    auto reads=r.roster_reads;
    ++r.me.incarnation;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1&&r.roster_reads==reads+1,"new raid owner reused the prior owner snapshot");
    reads=r.roster_reads;++r.player.zone_id;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1&&r.roster_reads==reads+1,"new zone reused the old display snapshot");
    reads=r.roster_reads;++r.player.zone_instance;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1&&r.roster_reads==reads+1,"new zone instance reused the old display snapshot");
    reads=r.roster_reads;r.hub.peer(false);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==-1,"inactive peer retained a map veto");
    r.hub.peer(true);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1&&r.roster_reads==reads+1,"peer activation reused a retired display snapshot");
    reads=r.roster_reads;r.hub.game_state(GAMESTATE_CHARSELECT);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"character selection retained raid marker admission");
    r.hub.game_state(GAMESTATE_INGAME);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1&&r.roster_reads==reads+1,"game-state transition reused a retired display snapshot");
    reads=r.roster_reads;r.hub.begin_zone();r.hub.game_state(GAMESTATE_INGAME);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1&&r.roster_reads==reads+1,"zone transition reused a retired display snapshot");
    reads=r.roster_reads;r.hub.set_host(&r.api);r.hub.peer(true);r.hub.game_state(GAMESTATE_INGAME);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1&&r.roster_reads==reads+1,"host rebinding reused a retired display snapshot");
    r.player_available=false;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"cached raid bypassed missing player context");
    r.player_available=true;r.spawn_available=false;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"cached raid bypassed missing own spawn");
    r.spawn_available=true;r.player.in_zone=0;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"cached raid bypassed current zone admission");
}
void map_raid_cache_failures() {
    Rig r;auto actor=raid_fixture(r);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"raid fixture unavailable");
    r.roster_available=false;r.hub.pulse();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"cached raid bypassed a failed roster read");
    auto reads=r.roster_reads;r.roster_available=true;r.hub.pulse();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1&&r.roster_reads==reads+1,"failed roster read did not retire the snapshot");
    r.partial_roster=true;r.hub.pulse();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"a partial bulk roster published raid admission");
    reads=r.roster_reads;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0&&r.roster_reads==reads,"a partial roster was retried per actor");
    r.partial_roster=false;r.hub.pulse();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1&&r.roster_reads==reads+1,"complete retry did not rebuild the failed snapshot");
    r.failed_member=r.raid[3].spawn_id;r.hub.pulse();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"unavailable member identity published raid admission");
    r.failed_member=0;r.truncated_member=r.raid[3].spawn_id;r.hub.pulse();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"truncated member identity published raid admission");
    r.truncated_member=0;r.raid[3].incarnation=0;r.hub.pulse();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"incomplete member identity published raid admission");
    r.raid[3].incarnation=1003;r.hub.pulse();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"a valid snapshot did not recover from invalid identity");
    r.raid[3].spawn_id=r.raid[2].spawn_id;r.hub.pulse();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"duplicate member IDs published raid admission");
    // Losing roster access only removes raid-based admission, not independent group/Tracking permissions.
    r.roster_available=false;r.player.class_id=4;r.hub.pulse();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"roster failure denied an independently eligible tracker PC marker");
    r.player.class_id=2;actor.in_group=1;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_FIND)==1,"raid display cache changed ordinary group Find");
}
void map_raid_cache_reentrancy() {
    Rig r;auto actor=raid_fixture(r);
    r.on_member=[](Rig& host) {
        host.on_member={};host.hub.peer(false);host.hub.peer(true);
    };
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"lifecycle reentry published the retired member fill");
    auto reads=r.roster_reads;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1&&r.roster_reads==reads+1,"retired fill was retained after lifecycle reentry");
    r.hub.pulse();
    r.on_roster=[actor](Rig& host) {
        host.on_roster={};
        check(host.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"recursive lookup admitted a partial cached roster");
    };
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"recursive roster callback did not invalidate outer admission");
    reads=r.roster_reads;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1&&r.roster_reads==reads+1,"recursive query retained the superseded snapshot");
    r.hub.pulse();
    r.on_roster=[](Rig& host) { host.on_roster={};host.hub.begin_zone(); };
    HailstoneMapSpawn mark{};mark.size=sizeof(mark);
    check(r.hub.map_spawn(&actor,&mark)==0,"zoning during a roster query painted a retired marker");
}
void map_lifecycle_and_server_find() {
    Rig r;auto actor=map_actor();actor.x=1;
    r.me.is_gm=1;actor.in_group=1;
    const auto incarnation=actor.incarnation;actor.incarnation=0;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"unknown actor identity bypassed policy through GM or group state");
    actor.incarnation=incarnation;
    const auto record_size=actor.size;actor.size=offsetof(HailstoneSpawn,incarnation);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"partial actor observation bypassed map policy");
    actor.size=record_size;
    check(r.hub.map_visible(nullptr,HAILSTONE_MAP_MARKER)==0,"missing actor observation failed open");
    check(r.hub.command("maps mobs off")&&r.hub.command("maps merchants off")&&r.hub.command("maps corpses off"),
          "GM filter fixture unavailable");
    actor.x=100000;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"GM bypassed an explicit disabled NPC category");
    check(r.hub.command("maps mobs on")&&r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,
          "GM lost range bypass for an enabled NPC category");
    r.me.is_gm=0;actor.in_group=0;r.player.class_id=2;r.tracking_skill=0;
    r.call("set","map_mobs",0);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"server Find changed marker admission");
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_FIND)==1,"server Find depended on map filters or Tracking");
    r.me.is_gm=1;r.call("set","map_mobs",1);
    actor.is_corpse=1;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"GM bypassed its disabled corpse category");
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_FIND)==0,"server Find admitted a corpse");
    actor.is_corpse=0;actor.in_group=0;
    r.hub.begin_zone();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0&&r.hub.map_visible(&actor,HAILSTONE_MAP_FIND)==0,
          "selected zoning peer abstained while a stale player snapshot remained usable");
    r.hub.game_state(GAMESTATE_INGAME);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"finishing zoning did not restore the selected peer policy");
    r.player_available=false;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0&&r.hub.map_visible(&actor,HAILSTONE_MAP_FIND)==0,
          "failed player observation failed open");
    r.player_available=true;r.spawn_available=false;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"failed own-spawn observation failed open");
    r.spawn_available=true;r.player.in_zone=0;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"missing in-zone state exposed another provider");
    r.player.in_zone=1;const auto getter=r.api.get_player;r.api.get_player=nullptr;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"missing player API failed open");
    r.api.get_player=getter;const auto spawn_getter=r.api.get_spawn;r.api.get_spawn=nullptr;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"missing own-spawn API failed open");
    r.api.get_spawn=spawn_getter;r.hub.peer(false);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==-1&&r.hub.map_visible(&actor,HAILSTONE_MAP_FIND)==-1,
          "leaving Bastion retained its presentation veto");
}
void map_group_filter_and_find() {
    Rig r;auto actor=map_actor();actor.is_npc=0;actor.in_group=1;actor.x=100000;
    r.player.class_id=2;r.tracking_skill=0;
    check(r.call("state").numbers.at("map_group")==1,"group filter did not default on");
    check(r.hub.command("maps group off"),"nontracker could not configure group markers");
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"disabled group marker escaped through nontracker policy");
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_FIND)==1,"group marker filter disabled group Find/navigation");
    const auto member=Rig::RaidMember{actor.id,actor.incarnation};
    r.raid.push_back(member);
    r.hub.pulse();
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"raid preference reintroduced a hidden group member");
    r.me.is_gm=1;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"GM bypass reintroduced a hidden group member");
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_FIND)==1,"GM group Find inherited the marker filter");
    actor.id=r.player.spawn_id;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"group filter hid the player itself");
    actor.id=member.spawn_id;actor.in_group=0;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"group filter hid a non-group raid member");
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_FIND)==1,"raid Find was changed by the group filter");
    actor.in_group=1;
    HailstoneMapSpawn mark{};mark.size=sizeof(mark);
    check(r.hub.map_spawn(&actor,&mark)==0,"group filter left the addon's own marker visible");
    r.call("set","map_group",1);
    check(r.hub.map_spawn(&actor,&mark)==1,"UI group filter could not restore the addon's own marker");
    r.call("set","map_group",0);r.hub.peer(false);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==-1,"disabled Bastion group provider vetoed another server");
}
void map_gm_category_choices() {
    Rig r;r.me.is_gm=1;r.player.class_id=2;r.tracking_skill=0;
    auto actor=map_actor();actor.x=100000;
    check(r.hub.command("maps mobs off")&&r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,
          "GM NPC switch did not hide ordinary NPCs");
    for(const auto class_id:{41u,61u,67u,68u,70u}) {
        actor.class_id=class_id;
        check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"GM NPC switch hid a separately enabled merchant class");
    }
    r.call("set","map_merchants",0);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"GM merchant switch did not hide merchants");
    r.call("set","map_mobs",1);actor.class_id=1;
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"enabled GM NPC switch did not bypass tracking/range");
    actor.is_corpse=1;r.call("set","map_corpses",0);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"GM corpse switch was ignored");
    r.call("set","map_corpses",1);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"enabled GM corpses still required tracking/range");
    actor.is_npc=0;actor.is_corpse=0;r.call("set","map_players",0);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"GM other-player switch was ignored");
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_FIND)==1,"GM player Find was disabled by a marker category");
    r.call("set","map_players",1);
    check(r.hub.map_visible(&actor,HAILSTONE_MAP_MARKER)==1,"enabled GM player marker required tracking");
}
void map_revision_and_settings_lifetime() {
    Rig r;
    const auto initial=map_revision(r.hub);
    for(int paint=0;paint<5;++paint) { r.hub.pulse();r.call("state"); }
    check(map_revision(r.hub)==initial,"passive paints advanced the map content revision");
    r.hub.command("maps group on");r.call("set","map_group",1);
    check(map_revision(r.hub)==initial,"unchanged map preference advanced its revision");
    r.hub.command("rightclicksell on");
    check(map_revision(r.hub)==initial,"merchant preference changed the map content revision");
    r.player.class_id=2;r.tracking_skill=0;
    r.call("set","map_mobs",0);
    check(r.call("state").numbers.at("map_mobs")==1&&map_revision(r.hub)==initial,
          "nontracker UI changed a tracking-gated preference or revision");
    r.hub.command("maps corpses off");
    check(r.call("state").numbers.at("map_corpses")==1&&map_revision(r.hub)==initial,
          "nontracker command changed a tracking-gated preference or revision");
    r.call("set","map_group",0);const auto changed=map_revision(r.hub);
    check(changed>initial,"UI group preference did not advance the map content revision");
    check(std::stoull(r.call("state").strings.at("map_revision"))==changed,"UI map revision disagrees with provider content");
    r.call("set","map_group",2);
    check(map_revision(r.hub)==changed&&r.call("state").numbers.at("map_group")==0,"invalid setting dirtied map state");
    bastion::CitadelHub restored;restored.set_host(&r.api);restored.peer(true);restored.game_state(GAMESTATE_INGAME);restored.pulse();
    auto actor=map_actor();actor.is_npc=0;actor.in_group=1;
    check(restored.map_visible(&actor,HAILSTONE_MAP_MARKER)==0,"saved group setting was not restored");
    check(map_revision(restored)>changed,"a later provider lifetime reused an older map content revision");
    r.hub.command("maps raid off");const auto command_revision=map_revision(r.hub);
    check(command_revision>changed,"nontracker raid command did not advance map content");
    const auto original=r.directory;r.directory=original/"another-character";r.hub.pulse();
    const auto new_character=map_revision(r.hub);
    check(new_character>command_revision&&r.call("state").numbers.at("map_group")==1,
          "character settings reload did not restore defaults and advance content");
    r.hub.pulse();check(map_revision(r.hub)==new_character,"unchanged character settings advanced content again");
    r.directory=original;r.hub.pulse();
    check(map_revision(r.hub)>new_character&&r.call("state").numbers.at("map_group")==0,
          "returning to a character did not restore its group preference");
    const auto before_transition=map_revision(r.hub);r.hub.begin_zone();r.hub.game_state(GAMESTATE_INGAME);
    check(map_revision(r.hub)==before_transition,"zoning without preference changes dirtied map content");
}
void map_retained_control_lifetime() {
    Rig r;r.call("set","map_group",0);const auto initial=map_revision(r.hub);
    r.hub.peer(false);r.call("set","map_group",1);
    check(r.call("state").numbers.at("map_group")==0,"retained filter control edited an inactive peer");
    r.hub.peer(true);check(map_revision(r.hub)==initial,"rejected inactive edit changed map content");
    r.hub.begin_zone();r.call("set","map_group",1);
    check(r.call("state").numbers.at("map_group")==0&&map_revision(r.hub)==initial,
          "retained filter control edited settings while zoning");
    const auto original=r.directory;r.directory=original/"another-peer";r.hub.peer(false);r.hub.pulse();
    r.call("set","map_group",0);
    check(r.call("state").numbers.at("map_group")==1&&!std::filesystem::exists(r.directory/"citadel.cfg"),
          "stale map control wrote another peer's settings file");
    r.directory=original;r.hub.peer(true);r.hub.game_state(GAMESTATE_INGAME);r.hub.pulse();
    r.call("set","map_group",1);
    check(r.call("state").numbers.at("map_group")==1,"fresh active map control could not restore its setting");
}
void map_policy_ownership() {
    Rig r;
    constexpr auto flags=HAILSTONE_MAP_POLICY_CUSTOM_SPAWNS|HAILSTONE_MAP_POLICY_HIDE_STOCK_NPCS;
    constexpr auto legacy_size=offsetof(HailstoneMapPolicy,npc_find_mode);
    HailstoneMapPolicy policy{};policy.size=sizeof(policy);
    check(r.hub.map_policy(&policy)==1&&policy.size==sizeof(policy)&&policy.flags==flags&&policy.name[sizeof(policy.name)-1]==0&&
          policy.npc_find_mode==HAILSTONE_NPC_FIND_SERVER_ONLY&&std::string(policy.name)=="Bastion",
          "selected Bastion did not own custom markers and allow server Find");
    policy={};policy.size=legacy_size;policy.npc_find_mode=0x12345678u;
    check(r.hub.map_policy(&policy)==1&&policy.size==legacy_size&&
          policy.flags==(flags|HAILSTONE_MAP_POLICY_DISABLE_NPC_FIND)&&policy.npc_find_mode==0x12345678u,
          "legacy host lost its map restrictions or the reply exceeded its buffer");
    r.hub.begin_zone();r.player_available=false;
    policy={};policy.size=sizeof(policy);
    check(r.hub.map_policy(&policy)==1&&policy.flags==flags&&
          policy.npc_find_mode==HAILSTONE_NPC_FIND_SERVER_ONLY&&std::string(policy.name)=="Bastion",
          "zoning or a missing player observation released Bastion map ownership");
    check(r.hub.map_policy(nullptr)==0,"null map policy output was accepted");
    policy.size=legacy_size-1;policy.flags=0x12345678u;
    check(r.hub.map_policy(&policy)==0&&policy.flags==0x12345678u,"undersized map policy output was changed");
    r.hub.peer(false);policy={};policy.size=sizeof(policy);policy.flags=0x12345678u;
    check(r.hub.map_policy(&policy)==0&&policy.flags==0x12345678u,
          "inactive map provider claimed ownership or changed an abstained output");
}
void sales() {
    Rig r; HailstoneMerchantSellContext c{}; c.size=sizeof(c); c.merchant_id=7; c.item_id=99; c.quantity=20; c.item_slot={0,24,3,-1};
    check(!r.hub.right_click_sell() && r.hub.merchant_right_click(&c)==0 && r.packet.empty(),"selling defaulted on");
    r.hub.command("rightclicksell on"); check(r.hub.merchant_right_click(&c)==1,"sale not handled");
    check(r.opcode==0x7d1b && r.packet.size()==20,"merchant wire envelope broken");
    hailstone::ByteReader b(r.packet.data(),r.packet.size());
    check(b.le<std::uint32_t>()==0x43535253 && b.le<std::uint32_t>()==1,"merchant wire header broken");
    check(b.le<std::uint32_t>()==4213 && b.le<std::uint32_t>()==20 && b.le<std::uint32_t>()==99,"bag slot or full stack corrupted");
    c.item_slot={0,24,-1,-1}; check(r.hub.merchant_right_click(&c)==1,"top slot rejected");
    c.item_slot.sub=200; check(!r.hub.merchant_right_click(&c),"invalid bag slot accepted");
    c.item_slot={0,13,-1,-1}; check(r.hub.merchant_right_click(&c)==1,"worn inventory sale parity lost");
    const auto saved=r.packet;
    c.flags=HAILSTONE_MERCHANT_CURSOR_OCCUPIED;
    check(r.hub.merchant_right_click(&c)==1 && saved==r.packet,"occupied cursor sold item or fell through");
    c.flags=HAILSTONE_MERCHANT_KEPT;
    check(r.hub.merchant_right_click(&c)==1 && saved==r.packet,"kept item sold or fell through");
    c.flags=HAILSTONE_MERCHANT_SELL_REFUSED;
    check(r.hub.merchant_right_click(&c)==1 && saved==r.packet,"refused sale fell through");
    r.hub.command("rightclicksell off"); check(!r.hub.right_click_sell(),"sale toggle not persistent");
    bastion::CitadelHub restored; restored.set_host(&r.api); restored.peer(true); restored.game_state(GAMESTATE_INGAME); restored.pulse();
    check(!restored.right_click_sell(),"saved merchant setting ignored");
}
}
void visibility_work() {
    Rig r;r.player_reads=r.directory_reads=0;
    for(unsigned i=0;i<120;++i) {
        const auto state=r.call("visibility");
        check(state.numbers.at("active")==1&&state.numbers.at("shown")==0,"hidden hub visibility changed admission");
        check(!state.numbers.contains("can_track")&&!state.numbers.contains("right_click_sell"),"hidden hub serialized full settings");
    }
    check(r.player_reads==0&&r.directory_reads==0&&r.packet.empty(),"hidden hub queried game/files or emitted a packet");
    r.hub.open("settings");check(r.call("visibility").numbers.at("shown")==1,"native hub open did not reach visibility");
    r.call("state");check(r.player_reads>0&&r.directory_reads>0,"visible hub did not obtain current full state");
    r.hub.begin_zone();check(r.call("visibility").numbers.at("active")==0,"zoning retained visible hub admission");
}
int main() { try { visibility_work(); maps(); map_tracking_policy(); map_filters_and_social_visibility(); map_raid_cache_scaling(); map_raid_cache_lifetime(); map_raid_cache_failures(); map_raid_cache_reentrancy(); map_lifecycle_and_server_find(); map_policy_ownership(); map_group_filter_and_find(); map_gm_category_choices(); map_revision_and_settings_lifetime(); map_retained_control_lifetime(); sales(); std::cout<<checks<<" Citadel checks passed\n"; return 0; } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
