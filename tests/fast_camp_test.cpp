// SPDX-License-Identifier: MIT
#include "fast_camp.h"
#include "citadel_test_host.h"
#include <chrono>
#include <filesystem>
#include <iostream>

struct HailstoneArgs {std::vector<HailstoneValue> values;};
struct HailstoneOut {std::string key;std::map<std::string,bool> booleans;std::map<std::string,std::string> strings;};
namespace fast=bastion::fast_camp;
using citadel_test::check;
namespace {
HailstoneValue text(const char* value) {HailstoneValue out{};out.kind=HAILSTONE_STRING;out.s=value;return out;}
HailstoneValue number(std::int64_t value) {HailstoneValue out{};out.kind=HAILSTONE_INT;out.i=value;return out;}
struct Rig:citadel_test::Host {
    fast::Adapter addon;
    std::filesystem::path scratch;
    std::string name="Fixture";
    std::uint32_t grants=HAILSTONE_CAP_READ|HAILSTONE_CAP_COMMANDS|HAILSTONE_CAP_ACTIONS|HAILSTONE_CAP_EXTEND|HAILSTONE_CAP_PACKETS;
    unsigned clock_reads=0,player_reads=0,spawn_reads=0,directory_reads=0,dialog_reads=0,answers=0;
    std::uint64_t map_context=UINT64_MAX-19;
    std::uint32_t map_size=sizeof(HailstoneMapPolicy);
    bool map_available=true;
    bool camping=false,allow_camp=true,has_dialog=false,answer_ok=true;
    HailstoneLargeDialog dialog{};
    std::vector<std::string> chat;
    std::function<void()> during_clock,during_dialog,during_answer,during_directory;
    static Rig& self(void* p) {return *static_cast<Rig*>(static_cast<citadel_test::Host*>(p));}
    Rig() {
        static unsigned serial=0;
        scratch=std::filesystem::temp_directory_path()/("bastion-fast-camp-test-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial));
        std::filesystem::create_directories(scratch);
        api.abi=HAILSTONE_PLUGIN_ABI;
        api.caps=[](void* p){return self(p).grants;};
        api.monotonic_ms=[](void* p){auto& r=self(p);++r.clock_reads;if(r.during_clock)r.during_clock();return r.now;};
        api.get_player=[](void* p,HailstonePlayer* out){auto& r=self(p);++r.player_reads;*out={};out->size=sizeof(*out);
            out->in_zone=r.in_zone?1:0;out->spawn_id=r.player;out->zone_id=r.zone;
            out->observation_flags=r.camping?HAILSTONE_PLAYER_CAMPING:0;
            std::copy(r.name.begin(),r.name.end(),out->name);return 1;};
        api.get_spawn=[](void* p,std::uint32_t id,HailstoneSpawn* out){auto& r=self(p);++r.spawn_reads;*out={};out->size=sizeof(*out);
            out->id=id;out->incarnation=r.incarnation;return 1;};
        api.get_map_policy=[](void* p,HailstoneMapPolicy* out){auto& r=self(p);*out={};out->size=r.map_size;
            out->context=r.map_context;return r.map_available?1:0;};
        api.character_config_dir=[](void* p,char* out,std::size_t size){auto& r=self(p);++r.directory_reads;
            const auto path=(r.scratch/r.name).string();if(r.during_directory)r.during_directory();
            if(size<=path.size())return -1;std::memcpy(out,path.c_str(),path.size()+1);return static_cast<int>(path.size());};
        api.server_info=[](void*,HailstoneServer* out){*out={};out->size=sizeof(*out);out->port=5999;
            std::memcpy(out->name,"Fixture",8);std::memcpy(out->host,"fixture.invalid",16);
            std::memcpy(out->world,"World",6);std::memcpy(out->world_host,"127.0.0.1",10);return 1;};
        api.do_command=[](void* p,const char* line){auto& r=self(p);r.commands.emplace_back(line);r.addon.observe_command(line);
            if(!r.allow_camp)return;
            if(r.camping){r.camping=false;return;}
            r.addon.outgoing(fast::camp_opcode);r.camping=true;};
        api.write_chat=[](void* p,const char* line,std::uint32_t){self(p).chat.emplace_back(line);};
        api.get_large_dialog=[](void* p,HailstoneLargeDialog* out){auto& r=self(p);++r.dialog_reads;
            check(out&&out->size==sizeof(*out),"dialog read lost the SDK output extent");
            if(!r.has_dialog)return 0;*out=r.dialog;if(r.during_dialog)r.during_dialog();return 1;};
        api.answer_large_dialog=[](void* p,std::uint64_t token,std::uint64_t camp,int button,std::uint32_t flags){auto& r=self(p);
            ++r.answers;check(token==r.dialog.token&&camp==r.dialog.camp_token&&button==1&&
                flags==HAILSTONE_DIALOG_EXPECT_CAMP_DISCONNECT,"fast camp changed tokens/button or requested an unsafe action");
            if(r.answer_ok)r.has_dialog=false;if(r.during_answer)r.during_answer();return r.answer_ok?1:0;};
        api.arg=[](void*,const HailstoneArgs* args,std::size_t i,HailstoneValue* out){if(i>=args->values.size())return 0;
            *out=args->values[i];return 1;};
        api.out_begin_table=[](void*,HailstoneOut*){};api.out_begin_array=[](void*,HailstoneOut*){};api.out_end=[](void*,HailstoneOut*){};
        api.out_key=[](void*,HailstoneOut* out,const char* key){out->key=key;};
        api.out_value=[](void*,HailstoneOut* out,const HailstoneValue* value){if(value->kind==HAILSTONE_BOOL)out->booleans[out->key]=value->i!=0;
            if(value->kind==HAILSTONE_STRING)out->strings[out->key]=value->s;};
        dialog.size=sizeof(dialog);dialog.flags=HAILSTONE_LARGE_DIALOG_YES_NO|HAILSTONE_LARGE_DIALOG_CAMPING;
        dialog.token=UINT64_MAX-1;dialog.camp_token=UINT64_MAX-3;dialog.popup_id=fast::accept_popup;
        dialog.negative_id=fast::decline_popup;dialog.remaining_ms=29000;
        addon.set_host(&api);addon.initialize();addon.peer(true);addon.game_state(GAMESTATE_INGAME);
    }
    ~Rig() {
        addon.shutdown();std::error_code ec;
        if(scratch.parent_path()==std::filesystem::temp_directory_path()&&scratch.filename().string().starts_with("bastion-fast-camp-test-"))
            std::filesystem::remove_all(scratch,ec);
    }
    HailstoneOut call(std::initializer_list<HailstoneValue> values) {
        HailstoneArgs args{values};HailstoneOut out;const auto fn=functions.at("fast_camp");
        check(fn.fn(fn.self,&args,&out)==1,"fast-camp UI callback failed");return out;
    }
    void tick(std::uint64_t delay=100) {now+=delay;addon.pulse();}
    void normal_camp() {addon.observe_command("/camp");addon.outgoing(fast::camp_opcode);camping=true;}
};
void explicit_offer_and_idle_budget() {
    Rig r;const auto clock=r.clock_reads,players=r.player_reads,spawns=r.spawn_reads,dirs=r.directory_reads;
    for(unsigned i=0;i<600;++i)r.addon.pulse();
    check(r.clock_reads==clock&&r.player_reads==players&&r.spawn_reads==spawns&&r.directory_reads==dirs&&
          r.dialog_reads==0&&r.answers==0&&r.commands.empty(),"idle addon performed host reads or an action");
    for(unsigned i=0;i<30;++i)check(!r.call({text("state")}).booleans.at("enabled"),"default fast camp was enabled");
    check(r.player_reads==players&&r.directory_reads==dirs&&r.commands.empty(),"passive settings paint performed character/file/action work");
    r.normal_camp();r.has_dialog=true;r.tick();check(r.answers==0&&!r.addon.armed(),"ordinary camp changed without opt-in");
    r.camping=false;check(r.addon.start("desktop")&&r.commands.back()=="/camp desktop"&&r.addon.armed(),"explicit fast camp lost native destination");
    r.addon.pulse();check(r.answers==1&&!r.addon.armed()&&r.sent.empty(),"eligible offer did not use exactly one native answer without raw packets");
    const auto polls=r.dialog_reads;for(unsigned i=0;i<300;++i)r.addon.pulse();
    check(r.answers==1&&r.dialog_reads==polls,"answered offer was replayed or polled after completion");
    check(r.addon.message().find("waiting for the server")!=std::string::npos,"local answer claimed server completion");
}
void unrelated_expired_and_cancelled_offers() {
    for(unsigned fault=0;fault<8;++fault) {
        Rig r;check(r.addon.start(),"offer fixture failed to start a camp");r.has_dialog=true;
        if(fault==0)++r.dialog.popup_id;if(fault==1)++r.dialog.negative_id;
        if(fault==2)r.dialog.token=0;if(fault==3)r.dialog.camp_token=0;
        if(fault==4)r.dialog.flags=HAILSTONE_LARGE_DIALOG_YES_NO;if(fault==5)r.dialog.flags=HAILSTONE_LARGE_DIALOG_CAMPING;
        if(fault==6)r.dialog.remaining_ms=0;if(fault==7)--r.dialog.size;
        r.addon.pulse();check(r.answers==0&&r.has_dialog,"unrelated/stale/malformed offer was consumed");
        r.now+=30000;r.addon.pulse();check(!r.addon.armed()&&r.answers==0,"unmatched offer request failed to expire");
    }
    Rig r;check(r.addon.start(),"cancel fixture failed to camp");r.camping=false;r.has_dialog=true;r.tick();
    check(!r.addon.armed()&&r.answers==0,"movement-cancelled camp accepted its outstanding offer");
    r.normal_camp();r.tick();check(r.answers==0&&!r.addon.armed(),"later normal camp inherited an old explicit request");
    Rig pending;check(pending.addon.start(),"poll fixture failed to start");pending.addon.pulse();const auto polls=pending.dialog_reads;
    for(unsigned i=0;i<99;++i)pending.tick(1);
    check(pending.dialog_reads==polls,"popup reads exceeded the100ms poll interval");pending.tick(1);
    check(pending.dialog_reads==polls+1,"eligible request was not polled at the next interval");
}
void preferences_permissions_and_context() {
    Rig r;check(r.call({text("set"),text("enabled"),number(1)}).booleans.at("ok")&&r.addon.enabled(),"opt-in setting was not saved");
    r.normal_camp();r.has_dialog=true;r.addon.pulse();check(r.answers==1,"opted-in normal camp did not accept its server offer");
    r.addon.begin_zone();r.name="Other";++r.incarnation;r.camping=false;r.addon.game_state(GAMESTATE_INGAME);
    check(!r.addon.enabled(),"character change inherited another character's opt-in");
    r.name="Fixture";++r.incarnation;r.addon.begin_zone();r.addon.game_state(GAMESTATE_INGAME);
    check(r.addon.enabled(),"original character's opt-in did not reload");
    for(const auto cap:{HAILSTONE_CAP_READ,HAILSTONE_CAP_ACTIONS,HAILSTONE_CAP_EXTEND,HAILSTONE_CAP_PACKETS,HAILSTONE_CAP_COMMANDS}) {
        Rig denied;denied.grants&=~cap;check(!denied.addon.start()&&denied.commands.empty()&&denied.answers==0,"missing permission started a fast camp");
    }
    for(unsigned change=0;change<5;++change) {
        Rig stale;check(stale.addon.start(),"scope fixture failed to start");stale.has_dialog=true;
        if(change==0)stale.addon.peer(false);if(change==1)stale.addon.begin_zone();
        if(change==2)++stale.incarnation;if(change==3)stale.grants&=~HAILSTONE_CAP_EXTEND;
        if(change==4)stale.grants&=~HAILSTONE_CAP_PACKETS;
        stale.tick();check(stale.answers==0&&!stale.addon.armed(),"retired scope or permission accepted a stale offer");
    }
    Rig old;old.api.size=offsetof(PluginHostApi,get_large_dialog);
    check(!old.addon.supported()&&!old.addon.start()&&old.commands.empty(),"old SDK host was treated as fast-camp capable");
    for(unsigned fault=0;fault<4;++fault) {
        Rig context;
        if(fault==0)context.api.get_map_policy=nullptr;
        if(fault==1)context.map_available=false;
        if(fault==2)context.map_size=offsetof(HailstoneMapPolicy,context);
        if(fault==3)context.map_context=0;
        check(!context.addon.start()&&context.commands.empty()&&context.answers==0,
              "missing, partial or zero host connection context admitted a fast-camp action");
    }
}
void strict_actions_and_reentrancy() {
    Rig already;already.camping=true;
    check(!already.addon.start()&&already.commands.empty()&&already.camping,"fast-camp request cancelled an existing camp");
    Rig invalid;check(!invalid.addon.start("desktop\n/camp")&&!invalid.addon.start(std::string(4096,'x'))&&invalid.commands.empty(),"invalid destination became a camp command");
    check(!invalid.call({text("start"),number(1)}).booleans.at("ok")&&invalid.commands.empty(),"invalid UI destination type became an empty camp request");
    check(invalid.addon.command("/fastcamp nonsense")&&invalid.chat.size()==1&&invalid.commands.empty(),"CLI error was silent, repeated or dispatched a camp");
    check(invalid.addon.command("/fastcamp\n/camp desktop")&&invalid.chat.size()==2&&invalid.commands.empty(),"multiline CLI request was not rejected once");
    Rig refused;refused.allow_camp=false;
    check(!refused.addon.start()&&!refused.addon.armed()&&refused.answers==0,"native camp refusal was represented as started");
    Rig clock;clock.during_clock=[&]{clock.addon.begin_zone();};
    check(!clock.addon.start()&&clock.commands.empty()&&!clock.addon.armed(),"clock callback moved a camp request across zone retirement");
    Rig settings;settings.name="Before";++settings.incarnation;
    const auto directory_reads=settings.directory_reads;settings.during_directory=[&]{settings.name="Changed";};
    check(!settings.addon.set_enabled(true)&&settings.directory_reads>directory_reads&&
          !std::filesystem::exists(settings.scratch/"Before"/"fast-camp.cfg")&&
          !std::filesystem::exists(settings.scratch/"Changed"/"fast-camp.cfg"),"reentrant character change saved settings in another scope");
    Rig replaced;check(replaced.addon.start(),"replacement fixture failed to camp");replaced.has_dialog=true;
    replaced.during_dialog=[&]{replaced.addon.peer(false);};replaced.addon.pulse();
    check(replaced.answers==0&&!replaced.addon.armed(),"dialog snapshot callback crossed peer retirement");
    Rig connection;check(connection.addon.start(),"connection-context fixture failed to camp");connection.has_dialog=true;
    connection.during_dialog=[&]{--connection.map_context;};connection.addon.pulse();
    check(connection.answers==0&&!connection.addon.armed(),
          "new connection's eligible dialog inherited old fast-camp intent with identical actor/zone/incarnation/name");
    Rig answered;check(answered.addon.start(),"answer fixture failed to camp");answered.has_dialog=true;
    answered.during_answer=[&]{answered.addon.begin_zone();};answered.addon.pulse();
    check(answered.answers==1&&!answered.addon.active()&&!answered.addon.armed(),"answer completion restored a retired camp request");
}
void lifecycle_messages_are_session_local() {
    for(unsigned change=0;change<4;++change) {
        Rig r;check(r.addon.set_enabled(true)&&r.addon.start(),"lifecycle fixture did not start an enabled camp");
        r.has_dialog=true;r.addon.pulse();
        check(r.answers==1&&r.addon.message().find("waiting for the server")!=std::string::npos,
              "lifecycle fixture did not reach its previous camp's pending-server message");
        r.addon.command("/fastcamp invalid-destination");
        check(!r.call({text("state")}).strings.at("error").empty(),"lifecycle fixture did not record a prior command error");
        const auto commands=r.commands.size(),chat=r.chat.size();
        if(change==0)r.addon.peer(false);
        if(change==1)r.addon.begin_zone();
        if(change==2)r.addon.game_state(GAMESTATE_CHARSELECT);
        if(change==3)r.addon.shutdown();
        check(r.addon.message().find("waiting for the server")==std::string::npos&&!r.addon.armed(),
              "retired lifecycle kept an active fast-camp status from the previous session");
        r.name="Other";++r.incarnation;--r.map_context;r.camping=false;
        if(change==3)r.addon.initialize();
        r.addon.peer(true);r.addon.game_state(GAMESTATE_INGAME);
        const auto current=r.call({text("state")});
        check(current.strings.at("message").find("Ordinary camping is unchanged")!=std::string::npos&&
              current.strings.at("error").empty()&&!current.booleans.at("enabled")&&!current.booleans.at("armed"),
              "new character/world inherited old active status, error or another character's saved opt-in");
        for(unsigned i=0;i<100;++i)r.addon.pulse();
        check(r.answers==1&&r.commands.size()==commands&&r.chat.size()==chat,
              "resetting lifecycle status repeated an answer, command or chat action");
    }
}
}
int main() {try {
    explicit_offer_and_idle_budget();unrelated_expired_and_cancelled_offers();preferences_permissions_and_context();strict_actions_and_reentrancy();
    lifecycle_messages_are_session_local();
    std::cout<<"Fast camp: "<<citadel_test::checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Fast camp after "<<citadel_test::checks<<" checks: "<<e.what()<<'\n';return 1;}}
