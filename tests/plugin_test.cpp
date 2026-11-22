#include "citadel_test_host.h"
#include "hailstone_byte_io.h"
#include <algorithm>
#include <iostream>
extern "C" std::uint32_t hailstone_plugin_abi();
extern "C" void hailstone_plugin_set_host(const PluginHostApi*);
extern "C" void InitializePlugin();
extern "C" void ShutdownPlugin();
extern "C" void SetGameState(int);
extern "C" void OnBeginZone();
extern "C" void OnPulse();
extern "C" int OnPacket(int,int,HailstonePacket*);
extern "C" const HailstoneHooks* hailstone_plugin_hooks();
using namespace citadel_test;
struct HailstoneArgs { std::vector<HailstoneValue> values; };
struct HailstoneOut {
    std::string key,panel,status,checksum;
    bool shown=false,requires_reconnect=false,legacy_submitted=false,accepted=false;
};
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
    check(hooks->command_line("/twist 1 2")==nullptr && hooks->command_line("/fastcamp")==nullptr,
          "server-specific song or camp commands activated before announcement");
    check(hooks->command_line("/hcl")==nullptr,"corpse shortcut activated before a Bastion announcement");
    constexpr auto policy_flags=HAILSTONE_MAP_POLICY_CUSTOM_SPAWNS|HAILSTONE_MAP_POLICY_HIDE_STOCK_NPCS;
    HailstoneMapPolicy policy{};policy.size=sizeof(policy);
    check(hooks->map_policy(&policy)==0&&policy.flags==0,"map ownership began before a Bastion announcement");
    HailstoneSpawn s{}; s.size=sizeof(s); s.id=9; s.incarnation=9; s.is_npc=1;
    check(hooks->map_visible(&s,0)==-1,"addon active before announcement");
    Bytes data(560); text(data,36,"BasDev"); HailstonePacket p{}; p.opcode=0x7ceb; p.data=reinterpret_cast<std::uint8_t*>(data.data()); p.size=data.size();
    OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_IN,&p);
    check(hooks->map_visible(&s,0)==0,"selected peer lost visibility policy");
    check(h.functions.contains("twist") && h.functions.contains("fast_camp"),"new module callbacks missing");
    for(const auto* line:{"/twist help","/sing 1","/stoptwist","/fastcamp","/citadel twist help","/bastion stoptwist","/cit fastcamp cancel"})
        check(hooks->command_line(line)!=nullptr,"song/camp command alias escaped addon routing");
    const auto corpse_alias=[&](const char* line,std::string_view expected) {
        const auto* replacement=hooks->command_line(line);
        check(replacement && replacement==expected,"corpse alias did not route to the native command");
    };
    corpse_alias(" /HCL\t","/hidecorpses looted");
    corpse_alias("/hca","/hidecorpses all");
    corpse_alias("/hcn","/hidecorpses none");
    corpse_alias("/hc","/hidecorpses");
    corpse_alias("/HideCo\tALWAYS","/hidecorpses ALWAYS");
    corpse_alias("/hidec allbutgroup","/hidecorpses allbutgroup");
    check(hooks->command_line("/hidecorpses npc")==nullptr && hooks->command_line("/hclong")==nullptr,
          "corpse aliases intercepted a native or unrelated command");
    const auto oversized=std::string("/hc ")+std::string(1012,'x');
    check(hooks->command_line(oversized.c_str())==nullptr && hooks->command_line("/hcl\n/hcn")==nullptr,
          "corpse aliases rewrote oversized or multiline input");
    check(hooks->map_policy(&policy)==1&&policy.flags==policy_flags&&
          policy.npc_find_mode==HAILSTONE_NPC_FIND_SERVER_ONLY&&policy.actor_guides==HAILSTONE_MAP_ACTOR_GUIDES&&
          policy.size==sizeof(policy)&&std::string(policy.name)=="Bastion",
          "selected peer did not publish its authoritative map presentation policy");
    policy.size=offsetof(HailstoneMapPolicy,actor_guides);
    policy.actor_guides=37;
    check(hooks->map_policy(&policy)==1&&policy.size==offsetof(HailstoneMapPolicy,actor_guides)&&
          policy.flags==policy_flags&&policy.npc_find_mode==HAILSTONE_NPC_FIND_SERVER_ONLY&&policy.actor_guides==37,
          "96-byte hosts lost server Find or received an out-of-bounds actor-guide field");
    policy.size=offsetof(HailstoneMapPolicy,npc_find_mode);
    policy.npc_find_mode=37;
    check(hooks->map_policy(&policy)==1&&policy.size==offsetof(HailstoneMapPolicy,npc_find_mode)&&
          policy.flags==(policy_flags|HAILSTONE_MAP_POLICY_DISABLE_NPC_FIND)&&policy.npc_find_mode==37&&policy.actor_guides==37,
          "88-byte hosts lost their NPC Find fallback or received out-of-bounds fields");
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
        if(out->key=="status" && value->kind==HAILSTONE_STRING) out->status=value->s;
        if(out->key=="legacy_checksum" && value->kind==HAILSTONE_STRING) out->checksum=value->s;
        if(out->key=="requires_reconnect" && value->kind==HAILSTONE_BOOL) out->requires_reconnect=value->i!=0;
        if(out->key=="legacy_submitted" && value->kind==HAILSTONE_BOOL) out->legacy_submitted=value->i!=0;
        if(out->key=="accepted" && value->kind==HAILSTONE_BOOL) out->accepted=value->i!=0;
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
    check(hooks->command_line("/twist 1 2")==nullptr && hooks->command_line("/fastcamp")==nullptr,
          "server-specific song or camp command survived a world switch");
    check(hooks->command_line("/hcl")==nullptr,"corpse shortcut survived a world switch");
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
    static_assert(offsetof(HailstoneMapPolicy,npc_find_mode)==88);
    static_assert(offsetof(HailstoneMapPolicy,actor_guides)==96);
    static_assert(sizeof(HailstoneMapPolicy)==104);
    struct PolicyReaderCase {
        const char* name;
        decltype(h.api.get_map_policy) reader;
        bool accepted;
    };
    const PolicyReaderCase reader_cases[]={
        {"full 104-byte map policy",policy_reader,true},
        {"legacy 96-byte policy with host context",[](void*,HailstoneMapPolicy* out) -> int {
            if (!legacy_map_policy_read(out,offsetof(HailstoneMapPolicy,actor_guides))) return 0;
            out->context=91;return 1;
        },true},
        {"legacy 88-byte policy with host context",[](void*,HailstoneMapPolicy* out) -> int {
            if (!legacy_map_policy_read(out,offsetof(HailstoneMapPolicy,npc_find_mode))) return 0;
            out->context=91;return 1;
        },true},
        {"full result without host context",[](void*,HailstoneMapPolicy* out) -> int {
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
        check(test.accepted ? owns_map==1&&policy.flags==policy_flags&&
                              policy.npc_find_mode==HAILSTONE_NPC_FIND_SERVER_ONLY&&std::string(policy.name)=="Bastion" :
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

    static unsigned measurements=0;
    h.api.abi=HAILSTONE_PLUGIN_ABI;
    h.api.measure_binary=[](void*,std::uint32_t kind,HailstoneBinaryMeasurement* out) -> int {
        ++measurements;
        check(kind==HAILSTONE_BINARY_CALLING_PLUGIN,"legacy route measured an unrelated binary");
        *out={};out->size=sizeof(*out);out->kind=kind;out->byte_count=123456;
        out->fnv1a63=0x123456789abcdef;out->sha256[0]=1;return 1;
    };
    server={"login.bastiongame.com","Bastion - Production",false};
    InitializePlugin();
    check(!active(),"initialization activated the addon without a login checkpoint");
    Bytes checksum(8);put(checksum,0,17,8);const auto original=checksum;
    p={};p.opcode=0x0f13;p.data=reinterpret_cast<std::uint8_t*>(checksum.data());p.size=checksum.size();
    const auto checkpoint=p;
    h.sent.clear();
    check(OnPacket(HAILSTONE_STREAM_ZONE,HAILSTONE_OUT,&p)==0&&measurements==0,
          "zone traffic triggered the WORLD DLL check");
    check(OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_IN,&p)==0&&measurements==0,
          "incoming traffic triggered the DLL check");
    h.during_send=[&] {
        HailstonePacket nested{};nested.opcode=0x7d1a;
        check(OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_OUT,&nested)==0,"addon consumed its own outgoing report");
        check(OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_OUT,&p)==0,"recursive checkpoint was consumed");
    };
    check(OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_OUT,&p)==0,"legacy report consumed the original CRC1");
    check(active()&&measurements==1&&h.sent.size()==1,"late login activation did not submit exactly one DLL report");
    check(p.opcode==checkpoint.opcode&&p.data==checkpoint.data&&p.size==checkpoint.size&&checksum==original,
          "legacy report modified the original checksum packet");
    check(h.sent[0].opcode==0x7d1a&&h.sent[0].bytes.size()==16,"legacy WORLD report has the wrong envelope");
    hailstone::ByteReader report(h.sent[0].bytes.data(),h.sent[0].bytes.size());
    check(report.le<std::uint32_t>()==0x43495431&&report.le<std::uint32_t>()==2&&
          report.le<std::uint64_t>()==0x123456789abcdef,"legacy route did not send the measured module checksum");
    h.during_send={};
    p.opcode=0x578f;
    check(OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_OUT,&p)==0&&measurements==1&&h.sent.size()==1,
          "EnterWorld repeated the DLL measurement or report");
    p.opcode=0x7a09;
    OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_OUT,&p);
    check(!active(),"new WORLD login retained the previous integrity session");
    p.opcode=0x578f;
    check(OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_OUT,&p)==0&&measurements==2&&h.sent.size()==2,
          "EnterWorld fallback failed to start the next login's DLL report");
    ShutdownPlugin();

    InitializePlugin();h.sent.clear();
    data.assign(560,std::byte{});text(data,36,"OtherWorld");
    p.opcode=0x7ceb;p.data=reinterpret_cast<std::uint8_t*>(data.data());p.size=data.size();
    OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_IN,&p);
    p.opcode=0x0f13;
    check(OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_OUT,&p)==0&&!active()&&measurements==2&&h.sent.empty(),
          "login fallback overrode an explicit unrelated WORLD announcement");
    ShutdownPlugin();
    server={"localhost","Bastion - Production",false};
    InitializePlugin();
    p.opcode=0x578f;
    check(OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_OUT,&p)==0&&!active()&&measurements==2&&h.sent.empty(),
          "legacy login fallback accepted an unrelated endpoint");
    ShutdownPlugin();

    const auto integrity_state=[&] {
        HailstoneValue operation{};operation.kind=HAILSTONE_STRING;operation.s="state";
        HailstoneArgs args{{operation}};HailstoneOut out;
        const auto function=h.functions.at("integrity");
        check(function.fn(function.self,&args,&out)==1,"integrity state callback became unavailable");
        return out;
    };
    const auto world_out=[&](std::uint16_t opcode,Bytes& body) {
        const auto original_body=body;
        HailstonePacket outgoing{opcode,reinterpret_cast<std::uint8_t*>(body.data()),body.size()};
        const auto original_packet=outgoing;
        check(OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_OUT,&outgoing)==0&&body==original_body&&
              outgoing.opcode==original_packet.opcode&&outgoing.data==original_packet.data&&outgoing.size==original_packet.size,
              "integrity login routing consumed or modified the caller's original WORLD packet");
    };
    const auto announce=[&] {
        Bytes announcement(560);text(announcement,36,"Bastion");
        HailstonePacket incoming{0x7ceb,reinterpret_cast<std::uint8_t*>(announcement.data()),announcement.size()};
        check(OnPacket(HAILSTONE_STREAM_WORLD,HAILSTONE_IN,&incoming)==0,"integrity routing consumed WORLD announcement");
    };
    server={"login.bastiongame.com","Bastion - Production",true};
    InitializePlugin();h.sent.clear();const auto late_measurements=measurements;
    check(active()&&integrity_state().requires_reconnect&&!integrity_state().legacy_submitted,
          "late in-zone load did not expose its missed fresh-login checkpoint");
    Bytes login(464);put(login,188,1,1);Bytes enter;
    world_out(0x7a09,login);SetGameState(GAMESTATE_PRECHARSELECT);announce();
    world_out(0x578f,enter);SetGameState(GAMESTATE_INGAME);h.now+=10000;OnPulse();
    check(measurements==late_measurements&&h.sent.empty()&&integrity_state().requires_reconnect,
          "late-load zoning fallback sent an integrity report that WORLD will ignore during transfer");
    put(login,188,0,1);world_out(0x7a09,login);SetGameState(GAMESTATE_PRECHARSELECT);announce();
    world_out(0x0f13,checksum);SetGameState(GAMESTATE_INGAME);
    const auto fresh=integrity_state();
    check(measurements==late_measurements+1&&h.sent.size()==1&&fresh.legacy_submitted&&!fresh.requires_reconnect&&
          !fresh.accepted&&fresh.checksum=="81985529216486895",
          "returning through full login did not submit and expose the exact calling-addon checksum");
    for(unsigned n=0;n<3;++n) {
        put(login,188,1,1);world_out(0x7a09,login);SetGameState(GAMESTATE_PRECHARSELECT);announce();
        world_out(0x578f,enter);SetGameState(GAMESTATE_INGAME);h.now+=10000;OnPulse();
        const auto current=integrity_state();
        check(measurements==late_measurements+1&&h.sent.size()==1&&current.legacy_submitted&&
              !current.requires_reconnect&&!current.accepted&&current.checksum==fresh.checksum&&current.status==fresh.status,
              "ordinary zoning retired, rehashed or retransmitted the original fresh-login receipt");
    }
    // Only the exact wire record and its actual zoning byte may preserve a receipt.
    for(const std::size_t size:{std::size_t{0},std::size_t{188},std::size_t{189},std::size_t{463}}) {
        Bytes truncated_login(size);
        if(size>188)put(truncated_login,188,1,1);
        const auto old_measurements=measurements;
        const auto old_sent=h.sent.size();
        world_out(0x7a09,truncated_login);announce();world_out(0x0f13,checksum);
        check(measurements==old_measurements+1&&h.sent.size()==old_sent+1,
              "an absent/truncated LoginInfo record was misread as a zoning login");
    }
    login.assign(464,std::byte{});put(login,187,1,1);
    const auto old_measurements=measurements;
    const auto old_sent=h.sent.size();
    world_out(0x7a09,login);announce();world_out(0x0f13,checksum);
    check(measurements==old_measurements+1&&h.sent.size()==old_sent+1,
          "a byte outside LoginInfo.zoning preserved an earlier login's receipt");
    ShutdownPlugin();
    std::cout<<checks<<" plugin checks passed\n"; return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
