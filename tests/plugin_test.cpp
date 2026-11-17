#include "citadel_test_host.h"
#include <algorithm>
#include <iostream>
extern "C" std::uint32_t hailstone_plugin_abi();
extern "C" void hailstone_plugin_set_host(const PluginHostApi*);
extern "C" void InitializePlugin();
extern "C" void ShutdownPlugin();
extern "C" void SetGameState(int);
extern "C" void OnBeginZone();
extern "C" int OnPacket(int,int,HailstonePacket*);
extern "C" const HailstoneHooks* hailstone_plugin_hooks();
using namespace citadel_test;
struct HailstoneArgs { std::vector<HailstoneValue> values; };
struct HailstoneOut { std::string key,panel; bool shown=false; };
int legacy_map_policy_read(HailstoneMapPolicy* out,std::size_t prefix) {
    if(!out||out->size<sizeof(out->size))return 0;
    const auto caller_size=out->size;
    HailstoneMapPolicy value{};value.size=static_cast<std::uint32_t>(prefix);value.revision=37;
    // Native give() copies its known prefix, then preserves the caller's requested extent.
    std::memcpy(out,&value,std::min(static_cast<std::size_t>(caller_size),prefix));
    out->size=caller_size;
    return 1;
}
int main() { try {
    check(hailstone_plugin_abi()==HAILSTONE_PLUGIN_ABI,"plugin export does not report the SDK ABI");
    Host binding;
    static unsigned binding_calls=0;
    binding.api.add_ui_function=[](void*,const char*,HailstoneUiFn,void*) { ++binding_calls;return 1; };
    binding.api.get_player=[](void*,HailstonePlayer*) { ++binding_calls;return 0; };
    binding.api.get_map_policy=[](void*,HailstoneMapPolicy*) { ++binding_calls;return 0; };
    binding.api.write_chat=[](void*,const char*,std::uint32_t) { ++binding_calls; };
    binding.api.log=[](void*,int,const char*) { ++binding_calls; };
    binding.api.do_command=[](void*,const char*) { ++binding_calls; };
    binding.api.send_packet=[](void*,int,std::uint16_t,const void*,std::size_t) { ++binding_calls;return 0; };
    hailstone_plugin_set_host(&binding.api);
    check(binding_calls==0&&binding.functions.empty()&&binding.commands.empty()&&binding.sent.empty(),
          "binding invoked host services before initialization");
    Host h;
    h.api.world_surface_patch=[](void*,const HailstoneWorldSurfacePatch*,HailstoneWorldSurfacePatchResult*) -> int { return HAILSTONE_WORLD_PATCH_DEFERRED; };
    h.api.measure_binary=[](void*,std::uint32_t,HailstoneBinaryMeasurement*) -> int { return 0; };
    h.api.get_map_policy=[](void*,HailstoneMapPolicy* out) -> int {
        if(!out||out->size<sizeof(*out))return 0;
        *out={};out->size=sizeof(*out);out->revision=37;out->context=91;return 1;
    };
    hailstone_plugin_set_host(&h.api);
    InitializePlugin(); SetGameState(GAMESTATE_INGAME);
    const auto* hooks=hailstone_plugin_hooks();
    check(hooks && hooks->map_visible && hooks->map_policy && hooks->merchant_right_click,"missing extension hooks");
    constexpr auto policy_flags=HAILSTONE_MAP_POLICY_CUSTOM_SPAWNS|HAILSTONE_MAP_POLICY_HIDE_STOCK_NPCS|
                                HAILSTONE_MAP_POLICY_DISABLE_NPC_FIND;
    HailstoneMapPolicy policy{};policy.size=sizeof(policy);
    check(hooks->map_policy(&policy)==0&&policy.flags==0,"map ownership began before a Bastion announcement");
    HailstoneSpawn s{}; s.size=sizeof(s); s.id=9; s.incarnation=9; s.is_npc=1;
    check(hooks->map_visible(&s,0)==-1,"addon active before announcement");
    Bytes data(560); text(data,36,"BasDev"); HailstonePacket p{}; p.opcode=0x7ceb; p.data=reinterpret_cast<std::uint8_t*>(data.data()); p.size=data.size();
    OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_IN,&p);
    check(hooks->map_visible(&s,0)==0,"selected peer lost visibility policy");
    check(hooks->map_policy(&policy)==1&&policy.flags==policy_flags&&std::string(policy.name)=="Bastion",
          "selected peer did not publish its authoritative map presentation policy");
    OnBeginZone();policy={};policy.size=sizeof(policy);
    check(hooks->map_policy(&policy)==1&&policy.flags==policy_flags,"zoning released the selected peer's stock-map restrictions");
    check(hooks->map_visible(&s,HAILSTONE_MAP_MARKER)==0&&hooks->map_visible(&s,HAILSTONE_MAP_FIND)==0,
          "zoning selected peer exposed a stale snapshot or another map provider");
    SetGameState(GAMESTATE_INGAME);
    const auto player_getter=h.api.get_player;
    h.api.get_player=[](void*,HailstonePlayer*) -> int { throw std::runtime_error("Injected map observation failure"); };
    check(hooks->map_visible(&s,HAILSTONE_MAP_MARKER)==0,"map hook exception escaped or failed open");
    h.api.get_player=player_getter;
    static unsigned moves=0;
    h.api.manual_follow=[](void*,const char*) { ++moves; return 1; };
    check(hooks->command_line("/bastion follow")!=nullptr && moves==0,"opening follow tool moved the player");
    h.api.arg=[](void*,const HailstoneArgs* args,std::size_t index,HailstoneValue* out) { if(index>=args->values.size()) return 0; *out=args->values[index]; return 1; };
    h.api.out_begin_table=[](void*,HailstoneOut*) {}; h.api.out_begin_array=[](void*,HailstoneOut*) {}; h.api.out_end=[](void*,HailstoneOut*) {};
    h.api.out_key=[](void*,HailstoneOut* out,const char* key) { out->key=key; };
    h.api.out_value=[](void*,HailstoneOut* out,const HailstoneValue* value) {
        if(out->key=="panel" && value->kind==HAILSTONE_STRING) out->panel=value->s;
        if(out->key=="shown" && value->kind==HAILSTONE_BOOL) out->shown=value->i!=0;
    };
    for(const auto* destination:{"settings","commands","tools"}) {
        HailstoneValue operation{},argument{}; operation.kind=argument.kind=HAILSTONE_STRING; operation.s="open"; argument.s=destination;
        HailstoneArgs args{{operation,argument}}; HailstoneOut out;
        const auto function=h.functions.at("citadel"); check(function.fn(function.self,&args,&out)==1,"hub action failed");
        check(out.shown && out.panel==destination,"tool card did not open its destination page");
    }
    check(!h.functions.contains("sentinel") && !h.functions.contains("nameplates"),"excluded addon bundled");
    Bytes zone(76); p.opcode=0x5089; p.data=reinterpret_cast<std::uint8_t*>(zone.data()); p.size=zone.size();
    OnPacket(HAILSTONE_STREAM_ZONE,HAILSTONE_OUT,&p); check(zone==Bytes(76),"spell negotiation duplicated");
    p.opcode=0x7d1d; check(!OnPacket(HAILSTONE_STREAM_ZONE,HAILSTONE_IN,&p),"nameplate stream consumed");
    data.assign(560,std::byte{}); text(data,36,"OtherWorld"); p.opcode=0x7ceb; p.data=reinterpret_cast<std::uint8_t*>(data.data()); p.size=data.size();
    OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_IN,&p); check(hooks->map_visible(&s,0)==-1,"world switch leaked policy");
    policy={};policy.size=sizeof(policy);
    check(hooks->map_policy(&policy)==0&&policy.flags==0,"non-Bastion world retained stock-map restrictions");
    ShutdownPlugin(); check(h.functions.empty(),"unload left UI callbacks");
    const auto policy_reader=h.api.get_map_policy;
    for(const auto table_size:{offsetof(PluginHostApi,get_raid_data_bulk),offsetof(PluginHostApi,get_map_policy),sizeof(PluginHostApi)}) {
        h.api.size=static_cast<std::uint32_t>(table_size);
        h.api.get_map_policy=table_size==sizeof(PluginHostApi)?nullptr:policy_reader;
        InitializePlugin(); SetGameState(GAMESTATE_INGAME);
        data.assign(560,std::byte{}); text(data,36,"BasDev"); p.data=reinterpret_cast<std::uint8_t*>(data.data()); p.size=data.size();
        OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_IN,&p);
        check(hooks->map_visible(&s,0)==-1,"host without usable map-policy service partially activated Bastion");
        policy={};policy.size=sizeof(policy);
        check(hooks->map_policy(&policy)==0&&policy.flags==0,"unsupported host retained a partial map presentation policy");
        ShutdownPlugin(); check(h.functions.empty(),"old host unload left callbacks");
    }
    static_assert(offsetof(HailstoneMapPolicy,revision)==72);
    static_assert(offsetof(HailstoneMapPolicy,context)==80);
    static_assert(sizeof(HailstoneMapPolicy)==88);
    struct PolicyReaderCase {
        const char* name;
        decltype(h.api.get_map_policy) reader;
        bool accepted;
    };
    const PolicyReaderCase reader_cases[]={
        {"full 88-byte map policy",policy_reader,true},
        {"full 88-byte result without host context",[](void*,HailstoneMapPolicy* out) -> int {
            *out={};out->size=sizeof(*out);out->revision=37;return 1;
        },false},
        {"original 72-byte prefix",[](void*,HailstoneMapPolicy* out) -> int {
            std::memset(out,0,offsetof(HailstoneMapPolicy,revision));
            out->size=static_cast<std::uint32_t>(offsetof(HailstoneMapPolicy,revision));return 1;
        },false},
        {"80-byte revision-only prefix",[](void*,HailstoneMapPolicy* out) -> int {
            std::memset(out,0,offsetof(HailstoneMapPolicy,context));
            out->size=static_cast<std::uint32_t>(offsetof(HailstoneMapPolicy,context));return 1;
        },false},
        {"legacy 72-byte give preserves caller size",[](void*,HailstoneMapPolicy* out) -> int {
            return legacy_map_policy_read(out,offsetof(HailstoneMapPolicy,revision));
        },false},
        {"legacy 80-byte give preserves caller size",[](void*,HailstoneMapPolicy* out) -> int {
            return legacy_map_policy_read(out,offsetof(HailstoneMapPolicy,context));
        },false},
        {"failed map-policy read",[](void*,HailstoneMapPolicy* out) -> int {
            out->size=sizeof(*out);return 0;
        },false},
        {"throwing map-policy reader",[](void*,HailstoneMapPolicy*) -> int {
            throw std::runtime_error("Injected map-policy reader failure");
        },false},
        {"full map-policy reader restored",policy_reader,true}
    };
    h.api.size=sizeof(h.api);
    for(const auto& test:reader_cases) {
        h.api.get_map_policy=test.reader;
        InitializePlugin();SetGameState(GAMESTATE_INGAME);
        data.assign(560,std::byte{});text(data,36,"BasDev");
        p.opcode=0x7ceb;p.data=reinterpret_cast<std::uint8_t*>(data.data());p.size=data.size();
        OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_IN,&p);
        const auto because=[&](const char* detail) { return std::string(test.name)+": "+detail; };
        check(hooks->map_visible(&s,HAILSTONE_MAP_MARKER)==(test.accepted?0:-1),
              because("map activation ignored the negotiated result extent").c_str());
        policy={};policy.size=sizeof(policy);
        const auto owns_map=hooks->map_policy(&policy);
        check(test.accepted ? owns_map==1&&policy.flags==policy_flags&&std::string(policy.name)=="Bastion" :
                              owns_map==0&&policy.flags==0,
              because("unsupported map reader left presentation ownership active").c_str());
        check((hooks->command_line("/bastion settings")!=nullptr)==test.accepted,
              because("command activation disagrees with map-policy service support").c_str());
        ShutdownPlugin();check(h.functions.empty(),because("unload left callbacks").c_str());
    }

    struct ServerFixture { const char* host; const char* world; bool already_in_zone; };
    static ServerFixture server{};
    h.api.server_info=[](void*,HailstoneServer* out) {
        std::copy_n(server.host,std::strlen(server.host)+1,out->host);
        std::copy_n(server.world,std::strlen(server.world)+1,out->world);
        return 1;
    };
    h.api.get_player=[](void*,HailstonePlayer* out) {
        out->game_state=server.already_in_zone?GAMESTATE_INGAME:GAMESTATE_PRECHARSELECT;
        out->in_zone=server.already_in_zone;
        return 1;
    };
    h.api.get_map_policy=policy_reader;
    const auto active = [&] { return hooks->command_line("/bastion settings")!=nullptr; };
    server={"login.bastiongame.com","Bastion - Production",true};
    InitializePlugin();
    check(active(),"late load in the Bastion production zone did not activate");
    data.assign(560,std::byte{});text(data,36,"OtherWorld");
    p.opcode=0x7ceb;p.data=reinterpret_cast<std::uint8_t*>(data.data());p.size=data.size();
    OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_IN,&p);SetGameState(GAMESTATE_INGAME);
    check(!active(),"fallback overrode an explicit non-Bastion announcement");
    ShutdownPlugin();

    server.already_in_zone=false;
    InitializePlugin();
    check(!active(),"late fallback ran before zone-in");
    SetGameState(GAMESTATE_INGAME);
    check(active(),"late fallback failed on the Bastion zone-in event");
    ShutdownPlugin();

    for(const auto& unrelated:{ServerFixture{"localhost","Bastion - Production",true},
                              ServerFixture{"login.bastiongame.com","Unrelated World",true}}) {
        server=unrelated;
        InitializePlugin();SetGameState(GAMESTATE_INGAME);
        check(!active(),"late fallback accepted an unrelated server");
        ShutdownPlugin();
    }
    std::cout<<checks<<" plugin checks passed\n"; return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
