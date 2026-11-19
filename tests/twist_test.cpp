// SPDX-License-Identifier: MIT
#include "twist.h"
#include "citadel_test_host.h"
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>

struct HailstoneArgs {std::vector<HailstoneValue> values;};
struct HailstoneOut {std::string key;std::map<std::string,std::string> strings;std::map<std::string,std::int64_t> numbers;};
using namespace citadel_test;
namespace twist=bastion::twist;
namespace {
struct Rig:Host {
    struct Song {HailstoneSpell spell{};int formula=0,cap=0;};
    twist::Adapter addon;
    std::map<int,Song> spells;
    std::map<std::string,std::pair<HailstoneTloFn,void*>> tlos;
    std::filesystem::path directory;
    std::vector<std::string> chats;
    std::string character="TwistFixture",server_name="Fixture";
    std::uint32_t permissions=UINT32_MAX,cls=8,mask=1u<<7,casting=0,level=60;
    std::uint64_t context=71;
    unsigned reads=0,directory_reads=0;
    bool dead=false,stunned=false,feigning=false,bar=false,ready=true,raw_fields=true,window_known=true,tlo_collision=false;
    int stand=100;
    std::function<void()> after_stop,during_player;
    Rig(std::filesystem::path reuse={}) {
        static std::atomic<unsigned> serial=0;
        directory=reuse.empty()?std::filesystem::temp_directory_path()/
            ("bastion-twist-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial)):reuse;
        std::filesystem::create_directories(directory);
        api.abi=HAILSTONE_PLUGIN_ABI;
        api.caps=[](void* p){return static_cast<Rig*>(p)->permissions;};
        api.get_player=[](void* p,HailstonePlayer* out) {
            auto& r=*static_cast<Rig*>(p);++r.reads;if(r.during_player)r.during_player();
            *out={};out->size=sizeof(*out);out->spawn_id=r.player;out->zone_id=r.zone;out->in_zone=r.in_zone;
            out->class_id=r.cls;out->class_mask=r.mask;out->level=r.level;out->casting_spell_id=r.casting;
            out->stand_state=static_cast<std::uint32_t>(r.stand);out->game_state=GAMESTATE_INGAME;
            std::copy_n(r.character.c_str(),std::min(r.character.size(),sizeof(out->name)-1),out->name);return 1;
        };
        api.get_spawn=[](void* p,std::uint32_t id,HailstoneSpawn* out) {
            auto& r=*static_cast<Rig*>(p);++r.reads;*out={};out->size=sizeof(*out);out->id=id;out->incarnation=r.incarnation;return 1;
        };
        api.get_map_policy=[](void* p,HailstoneMapPolicy* out) {
            auto& r=*static_cast<Rig*>(p);++r.reads;*out={};out->size=sizeof(*out);out->context=r.context;return r.context?1:0;
        };
        api.server_info=[](void* p,HailstoneServer* out) {
            const auto& r=*static_cast<Rig*>(p);*out={};out->size=sizeof(*out);
            std::copy_n(r.server_name.c_str(),std::min(r.server_name.size(),sizeof(out->name)-1),out->name);return 1;
        };
        api.character_config_dir=[](void* p,char* out,std::size_t size) {
            auto& r=*static_cast<Rig*>(p);++r.directory_reads;const auto path=r.directory.string();
            if(path.size()+1>size)return -1;
            std::memcpy(out,path.c_str(),path.size()+1);return static_cast<int>(path.size());
        };
        api.get_memorized_spell=[](void* p,std::uint32_t index) {
            auto& r=*static_cast<Rig*>(p);++r.reads;const auto found=r.spells.find(static_cast<int>(index)+1);
            return found==r.spells.end()?0u:found->second.spell.id;
        };
        api.get_spell=[](void* p,std::uint32_t id,HailstoneSpell* out) {
            auto& r=*static_cast<Rig*>(p);++r.reads;
            for(const auto& [gem,song]:r.spells) {static_cast<void>(gem);if(song.spell.id==id){*out=song.spell;return 1;}}
            return 0;
        };
        api.parse=[](void* p,const char* raw,char* out,std::size_t size) {
            auto& r=*static_cast<Rig*>(p);++r.reads;const std::string text(raw);std::string result="NULL";
            const auto boolean=[](bool b){return b?"TRUE":"FALSE";};
            if(text=="${Me.Dead}")result=boolean(r.dead);
            else if(text=="${Me.Stunned}")result=boolean(r.stunned);
            else if(text=="${Me.Feigning}")result=boolean(r.feigning);
            else if(text=="${Window[CastingWindow].Open}"&&r.window_known)result=boolean(r.bar);
            else if(text.starts_with("${Me.SpellReady["))result=boolean(r.ready&&!r.bar&&!r.casting);
            else if(text.starts_with("${Spell[")&&r.raw_fields) {
                for(const auto& [gem,song]:r.spells) {
                    static_cast<void>(gem);
                    const auto prefix="${Spell["+std::to_string(song.spell.id)+"].";
                    if(text==prefix+"DurationFormula}")result=std::to_string(song.formula);
                    if(text==prefix+"DurationCap}")result=std::to_string(song.cap);
                }
            }
            if(result.size()+1>size)return -1;
            std::memcpy(out,result.c_str(),result.size()+1);return static_cast<int>(result.size());
        };
        api.do_command=[](void* p,const char* raw) {
            auto& r=*static_cast<Rig*>(p);const std::string text(raw);r.addon.observe_command(text);r.commands.push_back(text);
            if(text=="/stopsong") {r.casting=0;if(r.after_stop)r.after_stop();}
            else if(text.starts_with("/cast ")) {const auto gem=std::stoi(text.substr(6));r.casting=r.spells.at(gem).spell.id;}
            else if(text=="/stand")r.feigning=false;
        };
        api.write_chat=[](void* p,const char* line,std::uint32_t) {static_cast<Rig*>(p)->chats.emplace_back(line);};
        api.add_tlo=[](void* p,const char* name,HailstoneTloFn fn,void* user) {
            auto& r=*static_cast<Rig*>(p);if(r.tlo_collision)return 0;r.tlos[name]={fn,user};return 1;
        };
        api.remove_tlo=[](void* p,const char* name){static_cast<Rig*>(p)->tlos.erase(name);};
        api.arg=[](void*,const HailstoneArgs* args,std::size_t index,HailstoneValue* out) {
            if(index>=args->values.size())return 0;
            *out=args->values[index];return 1;
        };
        api.out_begin_table=[](void*,HailstoneOut*){};api.out_begin_array=[](void*,HailstoneOut*){};api.out_end=[](void*,HailstoneOut*){};
        api.out_key=[](void*,HailstoneOut* out,const char* key){out->key=key;};
        api.out_value=[](void*,HailstoneOut* out,const HailstoneValue* v) {
            if(v->kind==HAILSTONE_STRING)out->strings[out->key]=v->s;
            if(v->kind==HAILSTONE_INT||v->kind==HAILSTONE_BOOL)out->numbers[out->key]=v->i;
        };
        for(int gem=1;gem<=12;++gem) {
            Song song;song.spell.size=sizeof(song.spell);song.spell.id=static_cast<std::uint32_t>(100+gem);
            song.spell.recast_time_ms=0;spells.emplace(gem,song);
        }
        addon.set_host(&api);addon.initialize();addon.peer(true);addon.game_state(GAMESTATE_INGAME);
        reads=directory_reads=0;
    }
    ~Rig() {addon.set_host(nullptr);std::error_code error;std::filesystem::remove_all(directory,error);}
    void command(std::string_view line) {check(addon.command(line),"twist alias was not consumed");}
    void advance(std::uint64_t ms=3300) {now+=ms;bar=false;addon.pulse();}
    int last_cast() const {
        for(auto i=commands.rbegin();i!=commands.rend();++i)if(i->starts_with("/cast "))return std::stoi(i->substr(6));
        return 0;
    }
    unsigned casts() const {return static_cast<unsigned>(std::count_if(commands.begin(),commands.end(),[](const auto& s){return s.starts_with("/cast ");}));}
    HailstoneOut ui(const char* operation="state",const char* key=nullptr,int value=0) {
        HailstoneValue op{};op.kind=HAILSTONE_STRING;op.s=operation;HailstoneArgs args{{op}};
        HailstoneValue k{},v{};k.kind=HAILSTONE_STRING;k.s=key;v.kind=HAILSTONE_INT;v.i=value;
        if(key) {args.values.push_back(k);if(std::string_view(operation)=="set")args.values.push_back(v);}
        const auto f=functions.at("twist");HailstoneOut out;
        check(f.fn(f.self,&args,&out)==1,"twist UI function failed");return out;
    }
};
void formulas() {
    const std::array<std::array<int,4>,15> cases={{{0,80,61,0},{1,80,61,31},{6,80,61,31},{2,80,61,37},
        {3,0,61,30},{4,0,61,40},{11,0,61,110},{12,8,61,8},{5,90,61,3},{7,0,61,61},
        {8,80,61,71},{9,80,61,80},{10,400,61,193},{13,99,61,59},{3600,0,61,6000}}};
    for(const auto& c:cases)check(twist::duration_ticks(c[0],static_cast<unsigned>(c[1]),static_cast<unsigned>(c[2]))==static_cast<unsigned>(c[3]),"Citadel duration formula changed");
    check(twist::duration_ticks(50,0,60)==UINT32_MAX,"permanent duration lost its sentinel");
    check(!twist::duration_ticks(999,20,60),"unknown duration formula was guessed");
    check(twist::duration_ticks(1,0,1)==0&&twist::duration_ticks(2,999,1)==1,"duration cap or upward rounding changed");
}
void ordered_rotation_and_idle_cost() {
    Rig r;for(int i=0;i<1000;++i)r.addon.pulse();
    check(r.reads==0&&r.directory_reads==0&&r.commands.empty(),"idle twist performed host or filesystem observations");
    r.command("/twist 1 2 1 12");r.addon.pulse();check(r.last_cast()==1&&r.addon.current()==1&&r.addon.next()==2,"first gem/next were incorrect");
    const auto reads=r.reads;for(int i=0;i<100;++i)r.addon.pulse();check(r.reads==reads,"sub-100ms pulses repeated observations");
    r.advance(3200);check(r.casts()==1,"twist cast before its 33-tenth deadline");
    r.advance(100);check(r.last_cast()==2,"gem2 failed at the exact deadline");
    r.advance();check(r.last_cast()==1,"duplicate gem order was lost");r.advance();check(r.last_cast()==12,"gem12 was treated as a clicky");
    HailstoneValue value{};const auto tlo=r.tlos.at("Twist");
    check(tlo.first(tlo.second,nullptr,"Current",nullptr,&value)==1&&value.i==12,"Twist.Current lost twelfth gem");
    r.command("/stoptwist silent");check(!r.addon.running()&&r.addon.queue()=="1 2 1 12","stoptwist cleared the queue");
    r.reads=r.directory_reads=0;for(int i=0;i<1000;++i)r.addon.pulse();check(!r.reads&&!r.directory_reads,"stopped twist polled the host");
    check(tlo.first(tlo.second,nullptr,"Next",nullptr,&value)==1&&value.i==0,"inactive Twist.Next was not zero");
    r.command("/twist start silent");r.addon.pulse();check(r.last_cast()==1,"start did not resume the saved cursor");
    r.command("/twist clear");check(!r.addon.running()&&r.addon.queue().empty(),"clear retained a live sequence");
}
void duration_recast_hold_and_once() {
    Rig long_song;long_song.spells[1].formula=3;long_song.spells[1].cap=10;
    long_song.command("/twist 1 2");long_song.addon.pulse();long_song.advance();long_song.advance();
    check(long_song.last_cast()==2&&long_song.casts()==3,"long song was recast before duration-adjust deadline");
    long_song.advance(50700);check(long_song.last_cast()==1,"long song did not return at delay+duration-adjust");
    long_song.command("/sing 1");long_song.addon.pulse();const auto held=long_song.casts();long_song.advance();
    check(long_song.casts()==held+1&&long_song.last_cast()==1,"hold incorrectly applied long-song rotation suppression");
    long_song.command("/twist");long_song.addon.pulse();check(long_song.addon.next()!=0,"empty command failed to resume after hold");
    Rig recast;recast.spells[1].spell.recast_time_ms=5000;recast.ui("set","recast",2);
    recast.command("/twist 1 2");recast.addon.pulse();recast.advance();recast.advance();
    check(recast.last_cast()==2,"recast timer ignored the cast delay and additional adjustment");
    recast.advance();check(recast.last_cast()==1,"recast timer never became eligible");
    Rig once;once.command("/twist 1 2");once.addon.pulse();once.command("/twist once 3 4");once.addon.pulse();
    check(once.last_cast()==3,"one-shot did not begin independently");
    once.command("/twist stop silent");once.command("/twist start silent");once.addon.pulse();
    check(once.last_cast()==4,"stopping/resuming one-shot lost its cursor");
    once.addon.incoming_chat("You miss a note, bringing your song to a close!");once.addon.pulse();
    check(once.last_cast()==4,"one-shot final interrupted song was not retried");
    once.advance();check(once.last_cast()==2&&once.addon.queue()=="1 2","one-shot lost the interrupted original position");
    Rig only;only.command("/twist once 5 6");only.addon.pulse();only.advance();only.advance();
    check(only.casts()==2&&!only.addon.running(),"one-shot without a saved queue became a permanent rotation");
    Rig one;one.spells[1].formula=3;one.spells[1].cap=10;one.command("/twist 1");one.addon.pulse();one.advance();
    check(one.casts()==2,"single-song twist did not preserve Citadel's continuous-repeat behavior");
}
void cast_safety_and_chat_retries() {
    Rig r;r.command("/twist 1 2");r.bar=true;r.addon.pulse();check(r.commands.empty(),"manual in-progress cast was interrupted");
    r.advance(100);check(r.last_cast()==1,"completed casting bar prevented twist progress");
    r.addon.incoming_chat("You haven't recovered yet...");r.addon.pulse();check(r.casts()==2&&r.last_cast()==1,"recovery failure did not retry the same gem");
    r.addon.incoming_chat("Your fixture spell is interrupted.");r.addon.pulse();check(r.casts()==3&&r.last_cast()==1,"interruption did not retry the current gem");
    r.addon.incoming_chat("You can't cast spells while stunned!");r.advance(999);check(r.casts()==3,"stun retry ignored its one-second backoff");
    r.advance(1);check(r.casts()==4,"stun retry failed at the deadline");
    r.stunned=true;r.advance();check(r.casts()==4,"stunned character attempted another cast");r.stunned=false;r.feigning=true;
    r.advance(100);check(r.commands.back()=="/stand"&&r.casts()==4,"feigning correction cast during the same observation");
    r.stand=110;r.advance(100);check(!r.addon.running(),"sitting did not stop twist");
    r.command("/twist start");r.stand=100;r.dead=true;r.addon.pulse();check(!r.addon.running(),"dead/hover character kept twisting");
    Rig old;old.raw_fields=false;old.command("/twist 1 2");old.addon.pulse();
    check(old.commands.empty()&&!old.addon.running()&&old.addon.error().find("update Hailstone")!=std::string::npos,"missing raw spell fields silently guessed duration or cast");
    Rig windows;windows.window_known=false;windows.command("/twist 1");windows.addon.pulse();
    check(windows.commands.empty()&&!windows.addon.running(),"missing casting window observation was treated as safe");
    Rig missing;missing.spells.erase(1);missing.command("/twist 1");missing.addon.pulse();check(!missing.addon.running()&&missing.commands.empty(),"empty gem cast instead of suspending");
}
void lifecycle_permissions_and_manual_control() {
    for(const auto* line:{"/cast 2","/melody 3 4","/stopsong","/stopcast"}) {
        Rig r;r.command("/twist 1 2");r.addon.pulse();const auto commands=r.commands.size();r.addon.observe_command(line);r.advance();
        check(!r.addon.running()&&r.commands.size()==commands,"manual casting command competed with the scheduler");
    }
    Rig zone;zone.command("/twist 1 2");zone.addon.pulse();zone.addon.begin_zone();++zone.context;zone.addon.game_state(GAMESTATE_INGAME);zone.advance();
    check(zone.casts()==1&&!zone.addon.running(),"zone transition resumed an old rotation automatically");
    zone.command("/twist start");zone.addon.pulse();check(zone.casts()==2,"explicit resume failed in a fresh zone context");
    zone.addon.peer(false);zone.advance();check(zone.addon.queue().empty()&&!zone.tlos.contains("Twist"),"peer retirement retained rotation or Twist TLO");
    Rig permission;permission.command("/twist 1");permission.addon.pulse();permission.permissions&=~HAILSTONE_CAP_ACTIONS;permission.advance();
    check(!permission.addon.running()&&permission.casts()==1,"action revocation failed to retire twist");
    permission.permissions=UINT32_MAX;permission.advance();check(permission.casts()==1,"restored permissions restarted an old rotation");
    Rig changed;changed.command("/twist 1");changed.addon.pulse();++changed.incarnation;changed.advance();check(!changed.addon.running(),"spawn id reuse inherited a twist");
    Rig reentrant;reentrant.command("/twist 1");reentrant.after_stop=[&]{reentrant.addon.peer(false);};reentrant.addon.pulse();
    check(reentrant.casts()==0&&!reentrant.addon.running(),"world retirement inside stopsong leaked a later cast");
    Rig unload;unload.command("/twist 1");unload.addon.pulse();unload.addon.set_host(nullptr);
    check(unload.commands.back()=="/stopsong"&&unload.tlos.empty()&&unload.functions.empty(),"host loss failed to stop its owned song or unregister callbacks");
    Rig stale;stale.command("/twist 1");stale.addon.pulse();++stale.context;const auto count=stale.commands.size();stale.addon.set_host(nullptr);
    check(stale.commands.size()==count,"host retirement stopped a song from a different lifetime");
    Rig transfer;transfer.command("/twist 1 2");transfer.addon.pulse();transfer.addon.begin_zone();
    transfer.addon.world_login(true);transfer.addon.peer(false);transfer.addon.peer(true);++transfer.context;
    transfer.addon.game_state(GAMESTATE_INGAME);transfer.advance();
    check(transfer.addon.queue()=="1 2"&&!transfer.addon.running()&&transfer.casts()==1,
          "known zoning peer callbacks lost the passive queue or resumed it automatically");
    transfer.command("/twist start");transfer.addon.pulse();check(transfer.casts()==2,"explicit resume failed after WORLD zoning callbacks");
    transfer.addon.begin_zone();transfer.addon.world_login(true);transfer.addon.peer(false);transfer.addon.peer(true);
    transfer.character="AnotherCharacter";transfer.addon.game_state(GAMESTATE_INGAME);
    check(transfer.addon.queue().empty(),"a zoning-shaped callback sequence lent a queue to another character");
    Rig world;world.command("/twist 1 2");world.addon.begin_zone();world.addon.world_login(true);world.addon.peer(false);world.addon.peer(true);
    world.server_name="AnotherWorld";world.addon.game_state(GAMESTATE_INGAME);
    check(world.addon.queue().empty(),"a zoning-shaped callback sequence lent a queue to another world");
    Rig fresh;fresh.command("/twist 1 2");fresh.addon.world_login(false);fresh.addon.peer(false);fresh.addon.peer(true);fresh.addon.game_state(GAMESTATE_INGAME);
    check(fresh.addon.queue().empty()&&!fresh.addon.running(),"fresh login retained a former session's queue");
}
void settings_tlo_and_paint_are_passive() {
    Rig r;r.cls=2;r.mask=0;r.command("/twist 1");check(!r.addon.running()&&!r.chats.empty(),"non-bard bypassed default eligibility or lost its CLI error");
    r.ui("set","allow_nonbard",1);r.command("/twist 1");r.addon.pulse();check(r.casts()==1,"explicit non-bard preference failed");
    r.ui("set","allow_nonbard",0);check(!r.addon.running(),"disabling non-bard preference left an ineligible rotation active");
    r.mask=1u<<7;r.command("/twist 1");r.addon.pulse();check(r.addon.running(),"Bard class-mask eligibility was ignored");
    r.ui("set","quiet",1);r.ui("set","delay",31);r.ui("set","adjust",2);r.ui("set","recast",4);
    r.command("/twist stop silent");r.reads=r.directory_reads=0;
    for(int i=0;i<500;++i)r.ui();
    check(!r.reads&&!r.directory_reads,"UI paint performed gameplay or directory observations");
    check(r.addon.settings().quiet&&r.addon.settings().delay==31&&r.addon.settings().adjust==2&&r.addon.settings().recast==4,"settings did not round-trip through UI");
    Rig loaded(r.directory);check(loaded.addon.settings().quiet&&loaded.addon.settings().delay==31&&!loaded.addon.running(),"persisted timing settings were lost or caused automatic casting");
    r.command("/twist stop silent");const auto queue=r.addon.queue();std::string excessive="/twist";for(int i=0;i<40;++i)excessive+=" 1";
    const auto messages=r.chats.size();r.command(excessive);
    check(!r.addon.running()&&r.addon.queue()==queue&&r.chats.size()==messages+1,"overflow command resumed or mutated an old rotation, or hid the error");
    r.command("/twist 21");check(!r.addon.running(),"disabled clicky entry was accepted");
    r.command("/twist set 21 30 10 item AA");check(r.chats.back().find("disabled")!=std::string::npos,"disabled item/AA configuration silently succeeded");
    check(!r.addon.command("/unrelated"),"twist consumed another command");
}
void lifecycle_status_belongs_to_the_current_character() {
    Rig r;r.command("/twist 21");check(!r.ui().strings.at("error").empty(),"invalid command fixture did not expose an error");
    r.addon.peer(false);auto state=r.ui();
    check(state.strings.at("error").empty()&&state.strings.at("message")=="Twist is idle.","peer retirement retained another session's status or error");
    r.addon.peer(true);r.addon.game_state(GAMESTATE_INGAME);r.command("/twist 1 2");r.command("/twist 21");
    r.addon.begin_zone();state=r.ui();
    check(state.strings.at("error").empty()&&state.strings.at("message").find("stopped")!=std::string::npos,
          "zoning displayed an old queued/error status instead of the passive saved queue");
    r.addon.world_login(true);r.addon.peer(false);r.addon.peer(true);r.character="NextCharacter";r.addon.game_state(GAMESTATE_INGAME);
    state=r.ui();check(state.strings.at("error").empty()&&state.strings.at("message")=="Twist is idle."&&state.strings.at("queue").empty(),
          "replacement character inherited a previous character's queue status");
}
} // namespace
int main() {try {
    formulas();ordered_rotation_and_idle_cost();duration_recast_hold_and_once();cast_safety_and_chat_retries();
    lifecycle_permissions_and_manual_control();settings_tlo_and_paint_are_passive();lifecycle_status_belongs_to_the_current_character();
    std::cout<<checks<<" twist checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Twist after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
